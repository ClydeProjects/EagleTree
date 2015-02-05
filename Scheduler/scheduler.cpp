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
	overdue_events(NULL),
	completed_events(),
	dependencies(),
	ssd(NULL),
	ftl(NULL),
	bm(NULL),
	migrator(NULL),
	dependency_code_to_LBA(),
	dependency_code_to_type(),
	safe_cache(0),
	stats()
{
	READ_TRANSFER_DEADLINE = PAGE_READ_DELAY;
}

void IOScheduler::init(Ssd* new_ssd, FtlParent* new_ftl, Block_manager_parent* new_bm, Migrator* new_migrator) {
	ssd = new_ssd;
	ftl = new_ftl;
	bm = new_bm;
	migrator = new_migrator;
	init();
}

void IOScheduler::init() {
	future_events = new event_queue();
	completed_events = new event_queue();
	Priorty_Scheme* ps;
	switch (SCHEDULING_SCHEME) {
		case 0: ps = new Fifo_Priorty_Scheme(this); break;
		case 1: ps = new Noop_Priorty_Scheme(this); break;
		case 2: ps = new Smart_App_Priorty_Scheme(this); break;
		case 3: ps = new gcRe_gcWr_Er_Re_Wr_Priorty_Scheme(this); break;
		case 4: ps = new Er_Wr_Re_gcRe_gcWr_Priorty_Scheme(this); break;
		case 5: ps = new We_Re_gcWr_E_gcR_Priorty_Scheme(this); break;
		case 6: ps = new Re_Er_Wr_Priorty_Scheme(this); break;
		case 7: ps = new Semi_Fifo_Priorty_Scheme(this); break;
		default: ps = new Semi_Fifo_Priorty_Scheme(this); break;
	}
	current_events = new Scheduling_Strategy(this, ssd, ps);
	overdue_events = new Scheduling_Strategy(this, ssd, new Fifo_Priorty_Scheme(this));
}

IOScheduler::~IOScheduler(){
	//stats.print();
	//VisualTracer::print_horizontally(100000);
	delete future_events;
	delete current_events;
	delete overdue_events;
	for (auto entry : dependencies) {
		for (auto event : entry.second) {
			delete event;
		}
		entry.second.clear();
	}
	dependencies.clear();
	delete bm;
	delete migrator;
}

// assumption is that all events within an operation have the same LBA
void IOScheduler::schedule_events_queue(deque<Event*> events) {
	assert(events.size() > 0);
	long logical_address = events.back()->get_logical_address();
	event_type type = events.back()->get_event_type();
	uint operation_code = events.back()->get_application_io_id();
	if (type != GARBAGE_COLLECTION && type != ERASE) {
		dependency_code_to_LBA[operation_code] = logical_address;
	}
	dependency_code_to_type[operation_code] = type;
	assert(dependencies.count(operation_code) == 0);
	dependencies[operation_code] = events;

	//printf("dependency_code_to_type size: %d\n", dependency_code_to_type.size());

	Event* first = dependencies[operation_code].front();
	assert(dependencies.size() > 0);
	dependencies[operation_code].pop_front();

	if (events.back()->is_original_application_io() && first->is_mapping_op() && first->get_event_type() == READ) {
		first->set_application_io_id(first->get_id());
		dependency_code_to_type[first->get_id()] = READ;
		dependency_code_to_LBA[first->get_id()] = first->get_logical_address();
		queue<uint> dependency;
		dependency.push(operation_code);
		op_code_to_dependent_op_codes[first->get_id()] = dependency;
	}
	future_events->push(first);
}

// TODO: make this not call the schedule_events_queue method, but simply put the event in future events
void IOScheduler::schedule_event(Event* event) {
	deque<Event*> eventVec;
	eventVec.push_back(event);
	schedule_events_queue(eventVec);
}

void IOScheduler::send_earliest_completed_events_back() {
	for (auto event : completed_events->get_soonest_events()) {
		ssd->register_event_completion(event);
	}
}

void IOScheduler::complete(Event* event) {
	if (event->is_original_application_io()) {
		completed_events->push(event);
	} else {
		ssd->register_event_completion(event);
	}
}

void IOScheduler::execute_soonest_events() {
	/*if (StatisticsGatherer::get_global_instance()->total_writes() > 1000001) {
		if (!current_events->empty()) current_events->print();
		if (!overdue_events->empty()) overdue_events->print();
		PRINT_LEVEL = 2;
	}*/
	if (current_events->empty() && overdue_events->empty() && !completed_events->empty()) {
		send_earliest_completed_events_back();
	}
	double current_time = get_current_time();
	double next_events_time = current_time + 1;
	update_current_events(current_time);

	while (current_time < next_events_time && (!current_events->empty() || !overdue_events->empty())) {
		if (!completed_events->empty() && current_time >= completed_events->get_earliest_time()) {
			send_earliest_completed_events_back();
			update_current_events(current_time);
			current_time = get_current_time();
			continue;
		}
		bool emp  = overdue_events->empty();
		double ear = overdue_events->get_earliest_time();
		if (!overdue_events->empty() && overdue_events->get_earliest_time() < next_events_time) {
			overdue_events->schedule();
		}
		if (!current_events->empty() && current_events->get_earliest_time() < next_events_time) {
			current_events->schedule();
		}
		update_current_events(current_time);
		current_time = get_current_time();
	}

}

// this is used to signal the SSD object when all events have finished executing
bool IOScheduler::is_empty() {
	return current_events->empty() && future_events->empty() && overdue_events->empty();
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

void IOScheduler::push(Event* event) {
	event_type t = event->get_event_type();
	double wait = event->get_bus_wait_time();
	/*if (		(t == READ_COMMAND && wait >= READ_DEADLINE)
			|| 	(t == READ_TRANSFER && wait >= READ_TRANSFER_DEADLINE)
			|| 	(t == WRITE && wait >= WRITE_DEADLINE)) {
		overdue_events->push(event);
		//event->print();
	}
	else*/
		current_events->push(event);
}

// refactor this method. Seems like the last else clause is unreachable
double IOScheduler::get_current_time() const {
	double t1 = current_events->get_earliest_time();
	double t2 = overdue_events->get_earliest_time();
	if (!current_events->empty() && !overdue_events->empty())
		return min(t1, t2);
	else if (!current_events->empty())
		return t1;
	else if (!overdue_events->empty())
		return t2;
	else if (!future_events->empty())
		return future_events->get_earliest_time();
	else
		return 0;
}

MTRand_int32 random_number_generator(42);

// Generates a number between 0 and limit-1, used by the random_shuffle in update_current_events()
ptrdiff_t random_range(ptrdiff_t limit) {
	return random_number_generator() % limit;
}

// goes through all the events that have just been submitted (i.e. bus_wait_time = 0)
// in light of these new events, see if any other existing pending events are now redundant
void IOScheduler::update_current_events(double current_time) {
	while (!future_events->empty() && ((current_events->empty() && overdue_events->empty()) || future_events->get_earliest_time() < current_time + 1) ) {
		vector<Event*> events = future_events->get_soonest_events();
		random_shuffle(events.begin(), events.end(), random_range); // Process events with same timestamp in random order to prevent imbalances
		for (uint i = 0; i < events.size(); i++) {
			Event* e = events[i];
			init_event(e);
		}
	}
	StatisticsGatherer::get_global_instance()->register_events_queue_length(current_events->size() + overdue_events->size(), current_time);
	Queue_Length_Statistics::register_queue_size(current_events->size() + overdue_events->size(), current_time);
}

void IOScheduler::handle(vector<Event*>& events) {
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		if (event == NULL) {
			continue;
		}
		handle(event);
	}
}

void IOScheduler::handle(Event* event) {

	event->increment_iteration_count();
	event_type type = event->get_event_type();
	if (type == WRITE || type == COPY_BACK) {
		handle_write(event);
	}
	else if (type == READ_COMMAND && event->is_flexible_read()) {
		handle_flexible_read(event);
	}
	else if (type == READ_COMMAND || type == COPY_BACK) {
		handle_read(event);
	}
	else if (type == TRIM) {
		execute_next(event);
	}
	else {
		handle_event(event);
	}
}

// executes read_commands, read_transfers and erases
void IOScheduler::handle_event(Event* event) {
	double time = bm->in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
	bool can_schedule = bm->can_schedule_on_die(event->get_address(), event->get_event_type(), event->get_application_io_id());
	if (!can_schedule) {
		event->incr_bus_wait_time(BUS_DATA_DELAY + BUS_CTRL_DELAY + time);
		push(event);
	}
	else if (time > 0) {
		event->incr_bus_wait_time(time);
		push(event);
	}
	else {
		execute_next(event);
	}
}

void IOScheduler::handle_read(Event* event) {

	if (event->get_application_io_id() == 1693276) {
		int i = 0;
		i++;
	}

	double time = bm->in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
	bool can_schedule = bm->can_schedule_on_die(event->get_address(), event->get_event_type(), event->get_application_io_id());

	if (event->get_application_io_id() == 1622620) {
		int i = 0;
		i++;
	}

	if (!can_schedule) {
		event->incr_bus_wait_time(BUS_DATA_DELAY + BUS_CTRL_DELAY + time);
		push(event);
	}
	else if (time > 0) {
		event->incr_bus_wait_time(time);
		push(event);
	}
	else if (ALLOW_DEFERRING_TRANSFERS) {
		execute_next(event);
	} else {
		/*event->print();
		Event* transfer = dependencies[event->get_application_io_id()].front();
		dependencies[event->get_application_io_id()].pop_front();
		setup_dependent_event(event, transfer);*/
		long app_code = event->get_application_io_id();
		execute_next(event);

		Event* transfer = current_events->find(app_code);
		current_events->remove(transfer);
		if (transfer == NULL) {
			transfer = overdue_events->find(app_code);
			overdue_events->remove(transfer);
		}
		execute_next(transfer);
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
	double wait_time = bm->in_how_long_can_this_write_be_scheduled(fr->get_current_time());
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
	Address addr = event->get_address();

	if (event->get_address().valid == NONE) {
		addr = bm->choose_write_address(*event);
	}
	try_to_put_in_safe_cache(event);
	double wait_time = bm->in_how_long_can_this_event_be_scheduled(addr, event->get_current_time(), WRITE);
	//double wait_time = bm->in_how_long_can_this_write_be_scheduled2(event->get_current_time());

	if (addr.valid == NONE && event->get_event_type() == COPY_BACK) {
		transform_copyback(event);
	}
	else if (addr.valid == NONE) {
		event->incr_bus_wait_time(BUS_DATA_DELAY + BUS_CTRL_DELAY);  // actually, we never know how long to wait here. Space might clear on any LUN on the SSD any time
		push(event);
	}
	else if (!bm->can_schedule_on_die(addr, event->get_event_type(), event->get_application_io_id())) {
		event->incr_bus_wait_time(wait_time + BUS_DATA_DELAY + BUS_CTRL_DELAY);
		push(event);
		assert(false);
	}
	else if (wait_time > 0) {
		event->incr_bus_wait_time(wait_time);
		push(event);
	}
	else {
		event->set_address(addr);
		//if (event->get_replace_address().valid == NONE) {
			ftl->set_replace_address(*event);
		//}
		assert(addr.page < BLOCK_SIZE);
		execute_next(event);
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
		ssd->get_package(event->get_address().package)->get_die(event->get_address().die)->clear_register();
		bm->register_register_cleared();
	} else if (event->get_event_type() == COPY_BACK) {
		ssd->get_package(event->get_replace_address().package)->get_die(event->get_replace_address().die)->clear_register();
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
			double diff = event->get_current_time() - e->get_current_time();
			e->incr_accumulated_wait_time(diff);
			e->incr_pure_ssd_wait_time(event->get_bus_wait_time() + event->get_execution_time());
			dependents.pop_front();
			e->set_noop(true);
			inform_FTL_of_noop_completion(e);
			complete(e);
		}
		if (event->is_garbage_collection_op() && event->get_event_type() != WRITE) {
			trigger_next_migration(event);
		}
		dependencies.erase(dependency_code);
		dependency_code_to_LBA.erase(dependency_code);
		dependency_code_to_type.erase(dependency_code);
		current_events->register_event_compeltion(event);
		overdue_events->register_event_compeltion(event);
		manage_operation_completion(event);
		inform_FTL_of_noop_completion(event);
		//ssd->register_event_completion(event);
		complete(event);
	}
}

void IOScheduler::inform_FTL_of_noop_completion(Event* event) {
	//static int c = 0;
	//printf("%d\n", c++);
	if (event->get_event_type() == READ_TRANSFER) {
		ftl->register_read_completion(*event, SUCCESS);
		if (event->is_garbage_collection_op()) {
			trigger_next_migration(event);
		}
	}
	else if (event->get_event_type() == WRITE) {
		ftl->register_write_completion(*event, SUCCESS);
	}
	migrator->get_garbage_collector()->register_event_completion(*event);
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

void IOScheduler::setup_dependent_event(Event* event, Event* dependent) {
	int dependency_code = event->get_application_io_id();
	LBA_currently_executing.erase(event->get_logical_address());
	//LBA_currently_executing[dependent->get_logical_address()] = dependent->get_application_io_id();
	dependent->set_application_io_id(dependency_code);
	double diff = event->get_current_time() - dependent->get_current_time();
	//dependent->incr_os_wait_time(event->get_os_wait_time());
	dependent->incr_accumulated_wait_time(diff);
	dependent->incr_pure_ssd_wait_time(event->get_bus_wait_time() + event->get_execution_time());
	if (dependent->get_current_time() != event->get_current_time()) {
		printf("%f\n", dependent->get_current_time());
		printf("%f\n", event->get_current_time());
		dependent->print();
		event->print();
	}
	assert(dependent->get_current_time() == event->get_current_time());
	dependent->set_noop(event->get_noop());

	// The dependent event might have a different LBA and type - record this in bookkeeping maps
	LBA_currently_executing[dependent->get_logical_address()] = dependent->get_application_io_id();
	dependency_code_to_LBA[dependency_code] = dependent->get_logical_address();
	dependency_code_to_type[dependency_code] = dependent->get_event_type();
	init_event(dependent);
}


enum status IOScheduler::execute_next(Event* event) {
	enum status result = ssd->issue(event);
	assert(result == SUCCESS);

	if (PRINT_LEVEL > 0  /*&& event->is_original_application_io() */ /*&& (event->get_event_type() == WRITE || event->get_event_type() == ERASE *//*|| event->get_event_type() == READ_TRANSFER)*/   /* && event->is_garbage_collection_op() && (event->get_event_type() == WRITE || event->get_event_type() == ERASE)*/ ) {
		event->print();
		if (event->is_flexible_read()) {
			//printf("FLEX\n");
		}
	}

	if (event->get_logical_address() == 17569) {
		//event->print();
	}
	if (event->get_logical_address() == 9181 && event->get_event_type() == WRITE) {
		//event->print();
	}

	/*if (event->is_garbage_collection_op() && event->get_event_type() == READ_TRANSFER && event->get_address().package == 2 && event->get_address().die == 1 && event->get_address().block == 991) {
		printf("%f  ", event->get_current_time());
		event->print();
	}*/


/*
	if (event->get_event_type() == WRITE && event->get_replace_address().package == 0 && event->get_replace_address().die == 0 && event->get_replace_address().block == 186) {
		event->print();
	}

	if (event->get_application_io_id() == 1063947) {
		int i = 0;
		i++;
	}*/

	/*if (StatisticsGatherer::get_global_instance()->total_writes() == 500000) {
		VisualTracer::print_horizontally(1000);
	}

	if (StatisticsGatherer::get_global_instance()->total_writes() == 1000000) {
		VisualTracer::print_horizontally(1000);
	}

	if (StatisticsGatherer::get_global_instance()->total_writes() == 1500000) {
		VisualTracer::print_horizontally(1000);
	}*/

	/*if (event->get_address().package == 0 && event->get_address().die == 1 && event->get_address().block == 255 && event->get_event_type() == WRITE) {
		event->print();
	}
	if (event->get_replace_address().package == 0 && event->get_replace_address().die == 1 && event->get_replace_address().block == 255 && event->get_event_type() == WRITE) {
		event->print();
	}*/


	handle_finished_event(event);

	/*if (event->get_id() == 2794555) {
		VisualTracer::print_horizontally(200);
		event->print();
		PRINT_LEVEL = 1;
		int i = 0;
		i++;
	}

	bm->update_next_possible_write_time();
*/
	if (event->get_event_type() == READ_TRANSFER && event->is_garbage_collection_op()) {
		trigger_next_migration(event);
	}

	int dependency_code = event->get_application_io_id();
	if (dependencies[dependency_code].size() > 0) {
		Event* dependent = dependencies[dependency_code].front();
		dependencies[dependency_code].pop_front();
		setup_dependent_event(event, dependent);
	} else {
		assert(dependencies.count(dependency_code) == 1);
		dependencies.erase(dependency_code);
		uint lba = dependency_code_to_LBA[dependency_code];
		if (event->get_event_type() != ERASE && !event->is_flexible_read()) {
			if (LBA_currently_executing.count(lba) == 0) {
				printf("Assertion failure LBA_currently_executing.count(lba = %d) = %d, concerning ", lba, LBA_currently_executing.count(lba));
				event->print();
			}
			//assert(LBA_currently_executing.count(lba) == 1);
			LBA_currently_executing.erase(lba);
			//assert(LBA_currently_executing.count(lba) == 0);
			//assert(dependency_code_to_LBA.count(dependency_code) == 1);
		}
		manage_operation_completion(event);
	}

	if (safe_cache.exists(event->get_logical_address())) {
		safe_cache.remove(event->get_logical_address());
		delete event;
	} else {
		complete(event);
		if (completed_events->size() >= MAX_SSD_QUEUE_SIZE) {
			send_earliest_completed_events_back();
			//printf("here, events back to ssd\n");
		}
	}
	return result;
}

//
void IOScheduler::manage_operation_completion(Event* event) {

	int dependency_code = event->get_application_io_id();
	dependency_code_to_LBA.erase(dependency_code);
	dependency_code_to_type.erase(dependency_code);
	while (op_code_to_dependent_op_codes.count(dependency_code) == 1 && op_code_to_dependent_op_codes[dependency_code].size() > 0) {
		uint dependent_code = op_code_to_dependent_op_codes[dependency_code].front();
		op_code_to_dependent_op_codes[dependency_code].pop();
		Event* dependant_event = dependencies[dependent_code].front();

		if (dependant_event->get_application_io_id() == 245479) {
			dependant_event->print();
		}

		double diff = event->get_current_time() - dependant_event->get_current_time();
		if (diff > 0) {
			dependant_event->incr_accumulated_wait_time(diff);
			dependant_event->incr_pure_ssd_wait_time(event->get_bus_wait_time() + event->get_execution_time());
		}
		dependencies[dependent_code].pop_front();
		init_event(dependant_event);
	}
	op_code_to_dependent_op_codes.erase(dependency_code);
}

void IOScheduler::handle_finished_event(Event *event) {
	if (event->get_event_type() == WRITE && event->get_application_io_id() == 1125450) {
		int i = 0;
		i++;
	}
	stats.register_IO_completion(event);
	VisualTracer::register_completed_event(*event);
	StatisticsGatherer::get_global_instance()->register_completed_event(*event);

	current_events->register_event_compeltion(event);
	overdue_events->register_event_compeltion(event);
	if (event->get_event_type() == WRITE || event->get_event_type() == COPY_BACK) {
		ftl->register_write_completion(*event, SUCCESS);
		bm->register_write_outcome(*event, SUCCESS);
		//StateTracer::print();
	} else if (event->get_event_type() == ERASE) {
		bm->register_erase_outcome(*event, SUCCESS);
		ftl->register_erase_completion(*event);
	} else if (event->get_event_type() == READ_COMMAND) {
		bm->register_read_command_outcome(*event, SUCCESS);
	} else if (event->get_event_type() == READ_TRANSFER) {
		ftl->register_read_completion(*event, SUCCESS);
		bm->register_read_transfer_outcome(*event, SUCCESS);
	} else if (event->get_event_type() == TRIM) {
		ftl->register_trim_completion(*event);
		bm->trim(*event);
	} else {
		printf("LOOK HERE ");
		event->print();
	}
	migrator->register_event_completion(event);
}

void IOScheduler::init_event(Event* event) {
	uint dep_code = event->get_application_io_id();
	event_type type = event->get_event_type();

	if (event->get_noop() && event->get_event_type() != GARBAGE_COLLECTION) {
		push(event);
		return;
	}

	if (event->is_flexible_read() && (type == READ_COMMAND || type == READ_TRANSFER)) {
		push(event);
	}
	else if (type == TRIM || type == READ_COMMAND || type == READ_TRANSFER || type == WRITE || type == COPY_BACK) {
		if (should_event_be_scheduled(event)) {
			push(event);
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
		bm->register_write_arrival(*event);
		try_to_put_in_safe_cache(event);
	}
	else if (type == TRIM && event->get_replace_address().valid == NONE) {
		ftl->set_replace_address(*event);
	}
	else if (type == GARBAGE_COLLECTION) {
		vector<deque<Event*> > migrations = migrator->migrate(event);
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
		dependencies.erase(event->get_application_io_id());
		dependency_code_to_type.erase(event->get_application_io_id());
		delete event;
	}
	else if (type == ERASE) {
		push(event);
	}
	else if (type == MESSAGE) {
		bm->receive_message(*event);
		completed_events->push(event);
	}
}


void IOScheduler::trigger_next_migration(Event* event) {
	if (!migrator->more_migrations(event)) {
		return;
	}
	deque<Event*> migration = migrator->trigger_next_migration(event);
	Event* first = migration.front();
	migration.pop_front();
	Event* second = migration.front();
	dependencies[first->get_application_io_id()] = migration;
	dependency_code_to_LBA[first->get_application_io_id()] = first->get_logical_address();
	dependency_code_to_type[first->get_application_io_id()] = second->get_event_type(); // = WRITE for normal GC, COPY_BACK for copy backs
	init_event(first);
	//first->incr_bus_wait_time(first->get_current_time() - event->get_current_time());
	if (event->get_address().get_block_id() != first->get_address().get_block_id()) {
		if (LBA_currently_executing.at(first->get_logical_address()) == first->get_application_io_id()) {
			LBA_currently_executing.erase(first->get_logical_address());
			bm->register_trim_making_gc_redundant(first);
		}
		else if (!first->get_noop()) {
			bm->register_trim_making_gc_redundant(first);
		}
		/*else {
			bm->register_trim_making_gc_redundant(first);
			printf("------------> \t\t\n");
			first->print();
		}*/

		first->set_noop(true);
		first->set_address(event->get_address());
	}
}

void IOScheduler::try_to_put_in_safe_cache(Event* write) {
	if (safe_cache.has_space() && !safe_cache.exists(write->get_logical_address()) && !write->is_garbage_collection_op() && write->is_original_application_io()) {
		safe_cache.insert(write->get_logical_address());
		Event* immediate_response = new Event(*write);
		//immediate_response->print();
		immediate_response->set_cached_write(true);
		push(immediate_response);
		//printf("putting into cache:  %d    %f\n", write->get_logical_address(), write->get_bus_wait_time());
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

	uint dependency_code_of_new_event = new_event->get_application_io_id();
	uint common_logical_address = new_event->get_logical_address();
	uint dependency_code_of_other_event = LBA_currently_executing[common_logical_address];

	Event * existing_event = current_events->find(dependency_code_of_other_event);
	if (existing_event == NULL) {
		existing_event = overdue_events->find(dependency_code_of_other_event);
	}
	//bool both_events_are_gc = new_event->is_garbage_collection_op() && existing_event->is_garbage_collection_op();
	//assert(!both_events_are_gc);

	event_type new_op_code = dependency_code_to_type[dependency_code_of_new_event];
	event_type scheduled_op_code = dependency_code_to_type[dependency_code_of_other_event];

	assert(new_op_code != TRIM);
	assert(scheduled_op_code != TRIM);
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
	if (new_event->get_logical_address() == 37796 || existing_event->get_logical_address() == 37796) {
		int i = 0;
		i++;
	}

	if (IS_FTL_PAGE_MAPPING && new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		promote_to_gc(existing_event);
		remove_current_operation(new_event);
		push(new_event); // Make sure the old GC READ is run, even though it is now a NOOP command
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
		if (existing_event->is_garbage_collection_op() && !existing_event->is_original_application_io() && !existing_event->is_mapping_op()) {
			bm->register_trim_making_gc_redundant(new_event);
		}
	}
	else if (!IS_FTL_PAGE_MAPPING && new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_event->is_garbage_collection_op() && scheduled_op_code == TRIM) {
		remove_current_operation(new_event);
		push(new_event);
		bm->register_trim_making_gc_redundant(new_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (existing_event != NULL && existing_event->is_garbage_collection_op() && (new_op_code == WRITE || new_op_code == TRIM)) {
		if (new_op_code == TRIM) {
			bm->register_trim_making_gc_redundant(new_event);
		}

		//promote_to_gc(new_event);
		//remove_current_operation(existing_event);
		make_dependent(new_event, dependency_code_of_other_event);
		//if (existing_event->get_event_type() == WRITE) {
		//	new_event->set_address(existing_event->get_address());
		//	new_event->set_replace_address(existing_event->get_replace_address());
		//}
		//LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}

	// if two writes are scheduled, the one before is irrelevant and may as well be cancelled
	else if (new_op_code == WRITE && scheduled_op_code == WRITE) {	// 1
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		//make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == WRITE && scheduled_op_code == READ && existing_event->is_mapping_op()) { // 2
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == COPY_BACK && scheduled_op_code == READ && existing_event != NULL) { // 3
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if there is a write, but before a read was scheduled, we should read first before making the write
	else if (new_op_code == WRITE && (scheduled_op_code == READ || scheduled_op_code == READ_COMMAND || scheduled_op_code == READ_TRANSFER)) { // 4
		//assert(false);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if there is a read, and a write is scheduled, then the contents of the write must be buffered, so the read can wait
	else if (new_op_code == READ && existing_event != NULL && existing_event->is_garbage_collection_op()) {  // 5
		new_event->set_noop(true);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == READ && (scheduled_op_code == WRITE || scheduled_op_code == COPY_BACK )) { // 6
		//remove_current_operation(new_event);
		//current_events->push_back(new_event);
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if there are two reads to the same address, there is no point reading the same page twice.
	else if ((new_op_code == READ || new_op_code == READ_COMMAND || new_op_code == READ_TRANSFER) && (scheduled_op_code == READ || scheduled_op_code == READ_COMMAND || scheduled_op_code == READ_TRANSFER)) { // 7
		//assert(false);
		make_dependent(new_event, dependency_code_of_other_event);
		if (!new_event->is_garbage_collection_op()) {
			//new_event->set_noop(true);
			remove_current_operation(new_event);
		}
	}
	// if a write is scheduled when a trim is received, we may as well cancel the write
	else if (new_op_code == TRIM && scheduled_op_code == WRITE) {  // 8
		remove_current_operation(existing_event);
		if (existing_event->is_garbage_collection_op()) {
			bm->register_trim_making_gc_redundant(new_event);
		}
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if a trim is scheduled, and a write arrives, may as well let the trim execute first
	else if (new_op_code == WRITE && scheduled_op_code == TRIM) {  // 9
		make_dependent(new_event, dependency_code_of_other_event);
	}
	// if a read is scheduled when a trim is received, we must still execute the read. Then we can trim
	else if (new_op_code == TRIM && (scheduled_op_code == READ || scheduled_op_code == READ_TRANSFER || scheduled_op_code == READ_COMMAND)) {  // 10
		make_dependent(new_event, dependency_code_of_other_event);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if something is to be trimmed, and a read is sent, invalidate the read
	else if ((new_op_code == READ || new_op_code == READ_TRANSFER || new_op_code == READ_COMMAND) && scheduled_op_code == TRIM) { // 1
		if (new_event->is_garbage_collection_op()) {
			bm->register_trim_making_gc_redundant(new_event);
			remove_current_operation(new_event);
		}
		//new_event->set_noop(true);
	} else {
		assert(false);
	}
}

IOScheduler::stats::stats() :
		write_recorder(), read_commands_recorder(), read_transfers_recorder(), erase_recorder()
{}

void IOScheduler::stats::register_IO_completion(Event* e) {
	if (e->get_event_type() == WRITE && e->is_original_application_io()) {
		//printf("write:  %d    ", e->get_iteration_count());
		//e->print();
		write_recorder.register_io(e);
	}
	else if (e->get_event_type() == WRITE) {
		gc_write_recorder.register_io(e);
	}
	else if (e->get_event_type() == READ_COMMAND && e->is_original_application_io()) {
		read_commands_recorder.register_io(e);
	}
	else if (e->get_event_type() == READ_COMMAND) {
		gc_read_commands_recorder.register_io(e);
	}
	else if (e->get_event_type() == READ_TRANSFER) {
		read_transfers_recorder.register_io(e);
	}
	else if (e->get_event_type() == ERASE) {
		erase_recorder.register_io(e);
	}
}

void IOScheduler::stats::print() {
	printf("iterations per write:  %f\n", write_recorder.get_iterations_per_io());
	printf("iterations per gc write:  %f\n", gc_write_recorder.get_iterations_per_io());
	printf("iterations per read commands:  %f\n", read_commands_recorder.get_iterations_per_io());
	printf("iterations per gc read commands:  %f\n", gc_read_commands_recorder.get_iterations_per_io());
	printf("iterations per read transfers:  %f\n", read_transfers_recorder.get_iterations_per_io());
	printf("iterations per erases:  %f\n", erase_recorder.get_iterations_per_io());
}

IOScheduler::stats::IO_type_recorder::IO_type_recorder() :
		total_iterations(0), total_IOs(0) {}

void IOScheduler::stats::IO_type_recorder::register_io(Event* e) {
	total_iterations += e->get_iteration_count();
	total_IOs++;
}

double IOScheduler::stats::IO_type_recorder::get_iterations_per_io() {
	return total_iterations / (double)total_IOs;
}
