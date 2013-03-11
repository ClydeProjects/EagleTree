/*
 * ssd_io_scheduler.cpp
 *
 *  Created on: Apr 15, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include <limits>
#include <algorithm> // random_shuffle

using namespace ssd;

#define WAIT_TIME 5;

IOScheduler::IOScheduler() :
	future_events(),
	current_events(NULL),
	dependencies(),
	ssd(NULL),
	ftl(NULL),
	bm(NULL),
	dependency_code_to_LBA(),
	dependency_code_to_type(),
	safe_cache(0)
{
}

void IOScheduler::set_all(Ssd* new_ssd, FtlParent* new_ftl, Block_manager_parent* new_bm) {
	ssd = new_ssd;
	ftl = new_ftl;
	bm = new_bm;
	current_events = BALANCEING_SCHEME ? new Balancing_Scheduling_Strategy(this, bm, ssd) : new Simple_Scheduling_Strategy(this, ssd);
}

IOScheduler::~IOScheduler(){
	delete current_events;
	map<uint, deque<Event*> >::iterator i = dependencies.begin();
	for (; i != dependencies.end(); i++) {
		deque<Event*>& d = (*i).second;
		for (uint j = 0; j < d.size(); j++) {
			delete d[j];
		}
		d.clear();
	}
	dependencies.clear();
	delete bm;
}

// assumption is that all events within an operation have the same LBA
void IOScheduler::schedule_events_queue(deque<Event*> events) {
	long logical_address = events.back()->get_logical_address();
	event_type type = events.back()->get_event_type();
	uint operation_code = events.back()->get_application_io_id();
	if (type != GARBAGE_COLLECTION && type != ERASE) {
		dependency_code_to_LBA[operation_code] = logical_address;
	}
	dependency_code_to_type[operation_code] = type;
	assert(dependencies.count(operation_code) == 0);
	dependencies[operation_code] = events;

	Event* first = dependencies[operation_code].front();
	dependencies[operation_code].pop_front();

	if (events.back()->is_original_application_io() && first->is_mapping_op() && first->get_event_type() == READ) {
		first->set_application_io_id(first->get_id());
		dependency_code_to_type[first->get_id()] = READ;
		dependency_code_to_LBA[first->get_id()] = first->get_logical_address();
		queue<uint> dependency;
		dependency.push(operation_code);
		op_code_to_dependent_op_codes[first->get_id()] = dependency;
	}
	future_events.push(first);
}

// TODO: make this not call the schedule_events_queue method, but simply put the event in future events
void IOScheduler::schedule_event(Event* event) {
	deque<Event*> eventVec;
	eventVec.push_back(event);
	schedule_events_queue(eventVec);
}

void IOScheduler::execute_soonest_events() {
	double current_time = get_current_time();
	double next_events_time = current_time + 1;
	update_current_events(current_time);
	while (current_time < next_events_time && !current_events->empty() ) {
		current_events->schedule();
		update_current_events(current_time);
		current_time = get_current_time();
	}
}

// this is used to signal the SSD object when all events have finished executing
bool IOScheduler::is_empty() {
	return !current_events->empty() || !future_events.empty();
}

double IOScheduler::get_soonest_event_time(vector<Event*> const& events) const {
	double earliest_time = events.front()->get_current_time();
	for (uint i = 1; i < events.size(); i++) {
		if (events[i]->get_current_time() < earliest_time) {
			earliest_time = events[i]->get_current_time();
		}
	}
	return earliest_time;
}

double IOScheduler::get_current_time() const {
	if (!current_events->empty())
		return current_events->get_earliest_time();
	else if (future_events.empty())
		return 0;
	else
		return future_events.get_earliest_time();
}

MTRand_int32 random_number_generator(42);

// Generates a number between 0 and limit-1, used by the random_shuffle in update_current_events()
ptrdiff_t random_range(ptrdiff_t limit) {
	return random_number_generator() % limit;
}

// goes through all the events that have just been submitted (i.e. bus_wait_time = 0)
// in light of these new events, see if any other existing pending events are now redundant
void IOScheduler::update_current_events(double current_time) {
	StatisticsGatherer::get_global_instance()->register_events_queue_length(current_events->size(), current_time);
	while (!future_events.empty() && (current_events->empty() || future_events.get_earliest_time() < current_time + 1) ) {
		vector<Event*> events = future_events.get_soonest_events();
		random_shuffle(events.begin(), events.end(), random_range); // Process events with same timestamp in random order to prevent imbalances
		for (uint i = 0; i < events.size(); i++) {
			Event* e = events[i];
			init_event(e);
		}
	}
}

void IOScheduler::handle(vector<Event*>& events) {
	//sort(events.begin(), events.end(), overall_wait_time_comparator);
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		event_type type = event->get_event_type();
		if (type == WRITE || type == COPY_BACK) {
			handle_write(event);
		}
		else if (type == READ_COMMAND && event->is_flexible_read()) {
			handle_flexible_read(event);
		}
		else if (type == TRIM) {
			execute_next(event);
		}
		else {
			handle_event(event);
		}
	}
}

// executes read_commands, read_transfers and erases
void IOScheduler::handle_event(Event* event) {
	double time = bm->in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
	bool can_schedule = bm->can_schedule_on_die(event->get_address(), event->get_event_type(), event->get_application_io_id());
	if (can_schedule && time == 0) {
		execute_next(event);
	}
	else {
		double bus_wait_time = can_schedule ? time : BUS_DATA_DELAY + BUS_CTRL_DELAY;
		event->incr_bus_wait_time(bus_wait_time);
		current_events->push(event);
	}
}

void IOScheduler::handle_flexible_read(Event* event) {
	Flexible_Read_Event* fr = dynamic_cast<Flexible_Read_Event*>(event);
	Address addr = bm->choose_flexible_read_address(fr);

	// Check if the logical address is locked
	ulong logical_address = fr->get_candidates_lba()[addr.package][addr.die];
	bool logical_address_locked = LBA_currently_executing.count(logical_address) == 1;
	if (addr.valid == PAGE && logical_address_locked) {
		uint dependency_code_of_other_event = LBA_currently_executing[logical_address];
		Event * existing_event = current_events->find(dependency_code_of_other_event);
		if (existing_event != NULL && existing_event->is_garbage_collection_op()) {
			fr->set_noop(true);
			fr->set_address(addr);
			fr->set_logical_address(existing_event->get_logical_address());
			dependencies[fr->get_application_io_id()].front()->set_logical_address(existing_event->get_logical_address());
			fr->register_read_commencement();
			make_dependent(fr, existing_event->get_application_io_id());
		} else {
			//fr->find_alternative_immediate_candidate(addr.package, addr.die);
			double wait_time = WAIT_TIME;
			fr->incr_bus_wait_time(wait_time);
			current_events->push(fr);
		}
		return;
	}

	if (addr.valid > NONE) {
		Address ftl_address = ftl->get_physical_address(logical_address);
		if (ftl_address.compare(addr) != PAGE) {
			fr->set_noop(true);
			fr->set_address(addr);
			fr->set_logical_address(logical_address);
			dependencies[fr->get_application_io_id()].front()->set_logical_address(fr->get_logical_address());
			fr->register_read_commencement();
			current_events->push(fr);
			return;
		}
	}
	double wait_time = bm->in_how_long_can_this_event_be_scheduled(addr, fr->get_current_time());
	if ( wait_time == 0 && !bm->can_schedule_on_die(addr, event->get_event_type(), event->get_application_io_id())) {
		wait_time = WAIT_TIME;
	}

	if (wait_time == 0) {
		fr->set_address(addr);
		fr->set_logical_address(logical_address);
		fr->register_read_commencement();
		dependencies[event->get_application_io_id()].front()->set_logical_address(event->get_logical_address());
		assert(addr.page < BLOCK_SIZE);
		execute_next(fr);
		//VisualTracer::get_instance()->print_horizontally(100);
	}
	else {
		fr->incr_bus_wait_time(wait_time);
		current_events->push(fr);
	}
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
void IOScheduler::handle_write(Event* event) {
	if (event->is_cached_write()) {

	}
	Address addr = bm->choose_write_address(*event);
	try_to_put_in_safe_cache(event);
	double wait_time = bm->in_how_long_can_this_event_be_scheduled(addr, event->get_current_time());
	if ( wait_time == 0 && !bm->can_schedule_on_die(addr, event->get_event_type(), event->get_application_io_id()))  {
		wait_time = BUS_DATA_DELAY + BUS_CTRL_DELAY;
		printf("warning from handle_write.");
	}
	if (wait_time == 0) {
		event->set_address(addr);
		ftl->set_replace_address(*event);
		assert(addr.page < BLOCK_SIZE);
		execute_next(event);
	}
	else {
		event->incr_bus_wait_time(wait_time);
		if (event->get_event_type() == COPY_BACK && addr.valid == NONE) {
			transform_copyback(event);
		}
		current_events->push(event);
	}
}

void IOScheduler::transform_copyback(Event* event) {
	event->set_event_type(READ_TRANSFER);
	event->set_address(event->get_replace_address());
	Event* write = new Event(WRITE, event->get_logical_address(), 1, event->get_current_time());
	write->set_garbage_collection_op(true);
	write->set_replace_address(event->get_replace_address());
	write->set_application_io_id(event->get_application_io_id());
	dependencies[event->get_application_io_id()].push_back(write);
	dependency_code_to_type[event->get_application_io_id()] = WRITE;
}

bool IOScheduler::should_event_be_scheduled(Event* event) {
	remove_redundant_events(event);
	uint la = event->get_logical_address();
	return LBA_currently_executing.count(la) == 1 && LBA_currently_executing[la] == event->get_application_io_id();
}


void IOScheduler::remove_current_operation(Event* event) {
	event->set_noop(true);
	if (event->get_event_type() == READ_TRANSFER) {
		ssd->getPackages()[event->get_address().package].getDies()[event->get_address().die].clear_register();
		bm->register_register_cleared();
	} else if (event->get_event_type() == COPY_BACK) {
		ssd->getPackages()[event->get_replace_address().package].getDies()[event->get_replace_address().die].clear_register();
		bm->register_register_cleared();
	}
}

void IOScheduler::handle_noop_events(vector<Event*>& events) {
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();

		uint dependency_code = event->get_application_io_id();
		deque<Event*>& dependents = dependencies[dependency_code];
		while (dependents.size() > 0) {
			Event *e = dependents.front();
			dependents.pop_front();
			ssd->register_event_completion(e);
		}
		dependencies.erase(dependency_code);
		dependency_code_to_LBA.erase(dependency_code);
		dependency_code_to_type.erase(dependency_code);
		manage_operation_completion(event);
		ssd->register_event_completion(event);
	}
}

void IOScheduler::promote_to_gc(Event* event_to_promote) {
	event_to_promote->set_garbage_collection_op(true);
	deque<Event*>& dependents = dependencies[event_to_promote->get_application_io_id()];
	for (uint i = 0; i < dependents.size(); i++){
		dependents[i]->set_garbage_collection_op(true);
	}
}

void IOScheduler::make_dependent(Event* dependent_event, uint independent_code/*Event* independent_event_application_io*/) {
	uint dependent_code = dependent_event->get_application_io_id();
	op_code_to_dependent_op_codes[independent_code].push(dependent_code);
	dependencies[dependent_code].push_front(dependent_event);
}

enum status IOScheduler::execute_next(Event* event) {
	enum status result = ssd->issue(event);

	if (PRINT_LEVEL > 0) {
		event->print();
		if (event->is_flexible_read()) {
			//printf("FLEX\n");
		}
	}

	handle_finished_event(event, result);
	if (result == SUCCESS) {
		int dependency_code = event->get_application_io_id();
		if (dependencies[dependency_code].size() > 0) {
			Event* dependent = dependencies[dependency_code].front();
			LBA_currently_executing.erase(event->get_logical_address());
			//LBA_currently_executing[dependent->get_logical_address()] = dependent->get_application_io_id();
			dependent->set_application_io_id(dependency_code);

			double diff = event->get_current_time() - dependent->get_current_time();
			//dependent->incr_os_wait_time(event->get_os_wait_time());
			dependent->incr_accumulated_wait_time(diff);
			dependent->incr_pure_ssd_wait_time(event->get_bus_wait_time() + event->get_execution_time());
			dependent->set_noop(event->get_noop());
			dependencies[dependency_code].pop_front();
			// The dependent event might have a different LBA and type - record this in bookkeeping maps
			LBA_currently_executing[dependent->get_logical_address()] = dependent->get_application_io_id();
			dependency_code_to_LBA[dependency_code] = dependent->get_logical_address();
			dependency_code_to_type[dependency_code] = dependent->get_event_type();
			init_event(dependent);
		} else {
			assert(dependencies.count(dependency_code) == 1);
			dependencies.erase(dependency_code);
			uint lba = dependency_code_to_LBA[dependency_code];
			if (event->get_event_type() != ERASE && !event->is_flexible_read()) {
				if (LBA_currently_executing.count(lba) == 0) {
					printf("Assertion failure LBA_currently_executing.count(lba = %d) = %d, concerning ", lba, LBA_currently_executing.count(lba));
					event->print();
				}
				assert(LBA_currently_executing.count(lba) == 1);
				LBA_currently_executing.erase(lba);
				assert(LBA_currently_executing.count(lba) == 0);
				assert(dependency_code_to_LBA.count(dependency_code) == 1);
			}
			manage_operation_completion(event);
		}
	} else {
		fprintf(stderr, "execute_event: Event failed! ");
		dependencies.erase(event->get_application_io_id()); // possible memory leak here, since events are not deleted
	}

	if (safe_cache.exists(event->get_logical_address())) {
		safe_cache.remove(event->get_logical_address());
		//printf("removing from cache:  %d\n", event->get_logical_address());
		delete event;
	} else {
		ssd->register_event_completion(event);
	}

	return result;
}

void IOScheduler::manage_operation_completion(Event* event) {
	int dependency_code = event->get_application_io_id();
	dependency_code_to_LBA.erase(dependency_code);
	dependency_code_to_type.erase(dependency_code);
	while (op_code_to_dependent_op_codes.count(dependency_code) == 1 && op_code_to_dependent_op_codes[dependency_code].size() > 0) {
		uint dependent_code = op_code_to_dependent_op_codes[dependency_code].front();
		op_code_to_dependent_op_codes[dependency_code].pop();
		Event* dependant_event = dependencies[dependent_code].front();
		double diff = event->get_current_time() - dependant_event->get_current_time();
		if (diff > 0) {
			dependant_event->incr_bus_wait_time(diff);
		}
		dependencies[dependent_code].pop_front();
		init_event(dependant_event);
	}
	op_code_to_dependent_op_codes.erase(dependency_code);
}

void IOScheduler::handle_finished_event(Event *event, enum status outcome) {
	if (outcome == FAILURE) {
		event->print();
		assert(false);
	}

	VisualTracer::get_instance()->register_completed_event(*event);

	if (event->get_event_type() == READ_COMMAND && event->is_original_application_io()) {
		//event->print();
		//VisualTracer::get_instance()->print_horizontally(2000);
	}

	/*if (event->is_original_application_io() && event->get_event_type() == WRITE && event->get_latency() > 3100 && !event->is_garbage_collection_op()) {
		VisualTracer::get_instance()->print_horizontally(20000);
		event->print();
		printf(" ");
	}*/

	StatisticsGatherer::get_global_instance()->register_completed_event(*event);

	current_events->register_event_completion(event);
	if (event->get_event_type() == WRITE || event->get_event_type() == COPY_BACK) {
		ftl->register_write_completion(*event, outcome);
		bm->register_write_outcome(*event, outcome);
		//StateTracer::print();
	} else if (event->get_event_type() == ERASE) {
		bm->register_erase_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_COMMAND) {
		bm->register_read_command_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_TRANSFER) {
		ftl->register_read_completion(*event, outcome);
		bm->register_read_transfer_outcome(*event, outcome);
	} else if (event->get_event_type() == TRIM) {
		ftl->register_trim_completion(*event);
		bm->trim(*event);
	} else {
		printf("LOOK HERE ");
		event->print();
	}
}

void IOScheduler::init_event(Event* event) {
	uint dep_code = event->get_application_io_id();
	event_type type = event->get_event_type();

	if (event->get_noop() && event->get_event_type() != GARBAGE_COLLECTION) {
		current_events->push(event);
		return;
	}

	if (event->is_flexible_read() && (type == READ_COMMAND || type == READ_TRANSFER)) {
		current_events->push(event);
	}
	else if (type == TRIM || type == READ_COMMAND || type == READ_TRANSFER || type == WRITE || type == COPY_BACK) {
		if (should_event_be_scheduled(event)) {
			current_events->push(event);
		} else if (PRINT_LEVEL >= 1) {
			printf("Event not scheduled: ");
			event->print();
		}
	}

	if (event->get_event_type() == READ) {
		event->set_event_type(READ_COMMAND);
		Event* read_transfer = new Event(*event);
		read_transfer->set_event_type(READ_TRANSFER);
		dependencies[dep_code].push_front(read_transfer);
		init_event(event);
	}
	else if ((type == READ_COMMAND || type == READ_TRANSFER) && !event->is_flexible_read()) {
		ftl->set_read_address(*event);
	}
	else if (type == WRITE) {
		//printf("new event  %d    %f\n", event->get_logical_address(), event->get_bus_wait_time());
		bm->register_write_arrival(*event);
		try_to_put_in_safe_cache(event);
//		ftl->set_replace_address(*event);
	}
	else if (type == TRIM) {
		ftl->set_replace_address(*event);
	}
	else if (type == GARBAGE_COLLECTION) {
		vector<deque<Event*> > migrations = bm->migrate(event);
		while (migrations.size() > 0) {
			// Pick first migration from deque
			deque<Event*> migration = migrations.back();
			migrations.pop_back();
			// Pick first event from migration
			Event* first = migration.front();
			migration.pop_front();
			Event* second = migration.front();
			dependencies[first->get_application_io_id()] = migration;
			dependency_code_to_LBA[first->get_application_io_id()] = first->get_logical_address();
			dependency_code_to_type[first->get_application_io_id()] = second->get_event_type(); // = WRITE for normal GC, COPY_BACK for copy backs
			init_event(first);
		}
		delete event;
	}
	else if (type == ERASE) {
		current_events->push(event);
	}
}

void IOScheduler::try_to_put_in_safe_cache(Event* write) {
	if (safe_cache.has_space() && !safe_cache.exists(write->get_logical_address()) && !write->is_garbage_collection_op() && write->is_original_application_io()) {
		safe_cache.insert(write->get_logical_address());
		Event* immediate_response = new Event(*write);
		//immediate_response->print();
		immediate_response->set_cached_write(true);
		current_events->push(immediate_response);
		printf("putting into cache:  %d    %f\n", write->get_logical_address(), write->get_bus_wait_time());
	}
}


void IOScheduler::remove_redundant_events(Event* new_event) {
	uint la = new_event->get_logical_address();
	if (LBA_currently_executing.count(la) == 0) {
		LBA_currently_executing[new_event->get_logical_address()] = new_event->get_application_io_id();
		return;
	}
	if (LBA_currently_executing.count(la) == 1 && LBA_currently_executing[la] == new_event->get_application_io_id()) {
		return;
	}

	if (new_event->get_event_type() == READ_COMMAND) {
		int i = 0;
		i++;
	}

	uint dependency_code_of_new_event = new_event->get_application_io_id();
	uint common_logical_address = new_event->get_logical_address();
	uint dependency_code_of_other_event = LBA_currently_executing[common_logical_address];
	Event * existing_event = current_events->find(dependency_code_of_other_event);

	//bool both_events_are_gc = new_event->is_garbage_collection_op() && existing_event->is_garbage_collection_op();
	//assert(!both_events_are_gc);

	event_type new_op_code = dependency_code_to_type[dependency_code_of_new_event];
	event_type scheduled_op_code = dependency_code_to_type[dependency_code_of_other_event];

	//if (existing_event == NULL) {
		//new_event->print();
	//}
	//assert (existing_event != NULL || scheduled_op_code == COPY_BACK);

	// if something is to be trimmed, and a copy back is sent, the copy back is unnecessary to perform;
	// however, since the copy back destination address is already reserved, we need to use it.
	/*if (scheduled_op_code == COPY_BACK) {
		make_dependent(new_event, dependency_code_of_other_event);
		assert(false);
	}
	else if (new_op_code == COPY_BACK) {
		assert(false);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		make_dependent(existing_event, dependency_code_of_new_event);
		remove_event_from_current_events(existing_event); // Remove old event from current_events; it's added again when independent event (the copy back) finishes
	}
	else */

	if (new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		promote_to_gc(existing_event);
		remove_current_operation(new_event);
		current_events->push(new_event); // Make sure the old GC READ is run, even though it is now a NOOP command
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (new_event->is_garbage_collection_op() && scheduled_op_code == TRIM) {
		remove_current_operation(new_event);
		current_events->push(new_event);
		bm->register_trim_making_gc_redundant();
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (existing_event != NULL && existing_event->is_garbage_collection_op() && (new_op_code == WRITE || new_op_code == TRIM)) {
		if (new_op_code == TRIM) {
			bm->register_trim_making_gc_redundant();
		}

		promote_to_gc(new_event);
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}

	// if two writes are scheduled, the one before is irrelevant and may as well be cancelled
	else if (new_op_code == WRITE && scheduled_op_code == WRITE) {
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		//make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == WRITE && scheduled_op_code == READ && existing_event->is_mapping_op()) {
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == COPY_BACK && scheduled_op_code == READ && existing_event != NULL) {
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if there is a write, but before a read was scheduled, we should read first before making the write
	else if (new_op_code == WRITE && (scheduled_op_code == READ || scheduled_op_code == READ_COMMAND || scheduled_op_code == READ_TRANSFER)) {
		//assert(false);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if there is a read, and a write is scheduled, then the contents of the write must be buffered, so the read can wait
	else if (new_op_code == READ && existing_event != NULL && existing_event->is_garbage_collection_op()) {
		new_event->set_noop(true);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == READ && (scheduled_op_code == WRITE || scheduled_op_code == COPY_BACK )) {
		//remove_current_operation(new_event);
		//current_events->push_back(new_event);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if there are two reads to the same address, there is no point reading the same page twice.
	else if ((new_op_code == READ || new_op_code == READ_COMMAND || new_op_code == READ_TRANSFER) && (scheduled_op_code == READ || scheduled_op_code == READ_COMMAND || scheduled_op_code == READ_TRANSFER)) {
		//assert(false);
		make_dependent(new_event, dependency_code_of_other_event);
		if (!new_event->is_garbage_collection_op()) {
			//new_event->set_noop(true);
			remove_current_operation(new_event);
		}
	}
	// if a write is scheduled when a trim is received, we may as well cancel the write
	else if (new_op_code == TRIM && scheduled_op_code == WRITE) {
		remove_current_operation(existing_event);
		if (existing_event->is_garbage_collection_op()) {
			bm->register_trim_making_gc_redundant();
		}
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if a trim is scheduled, and a write arrives, may as well let the trim execute first
	else if (new_op_code == WRITE && scheduled_op_code == TRIM) {
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if a read is scheduled when a trim is received, we must still execute the read. Then we can trim
	else if (new_op_code == TRIM && (scheduled_op_code == READ || scheduled_op_code == READ_TRANSFER || scheduled_op_code == READ_COMMAND)) {
		make_dependent(new_event, dependency_code_of_other_event);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if something is to be trimmed, and a read is sent, invalidate the read
	else if ((new_op_code == READ || new_op_code == READ_TRANSFER || new_op_code == READ_COMMAND) && scheduled_op_code == TRIM) {
		if (new_event->is_garbage_collection_op()) {
			bm->register_trim_making_gc_redundant();
			remove_current_operation(new_event);
		}
		//new_event->set_noop(true);
	} else {
		assert(false);
	}
}
