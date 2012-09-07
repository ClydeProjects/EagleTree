/*
 * ssd_io_scheduler.cpp
 *
 *  Created on: Apr 15, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <limits>

using namespace ssd;

IOScheduler::IOScheduler(Ssd& ssd,  FtlParent& ftl) :
	ssd(ssd),
	ftl(ftl),
	dependency_code_to_LBA(),
	dependency_code_to_type(),
	stats()
{
	if (BLOCK_MANAGER_ID == 0) {
		bm = new Block_manager_parallel(ssd, ftl);
	} else if (BLOCK_MANAGER_ID == 1) {
		bm = new Block_manager_parallel_hot_cold_seperation(ssd, ftl);
	} else if (BLOCK_MANAGER_ID == 2) {
		bm = new Wearwolf(ssd, ftl);
	} else if (BLOCK_MANAGER_ID == 3) {
		bm = new Wearwolf_Locality(ssd, ftl);
	} else if (BLOCK_MANAGER_ID == 4) {
		bm = new Block_manager_roundrobin(ssd, ftl);
	} else {
		assert(false);
	}
}

IOScheduler::~IOScheduler(){
	delete bm;
}

IOScheduler *IOScheduler::inst = NULL;

void IOScheduler::instance_initialize(Ssd& ssd, FtlParent& ftl)
{
	IOScheduler::inst = new IOScheduler(ssd, ftl);
}

IOScheduler *IOScheduler::instance()
{
	return IOScheduler::inst;
}

bool bus_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
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

	future_events.push_back(first);
}

// TODO: make this not call the schedule_events_queue method, but simply put the event in future events
void IOScheduler::schedule_event(Event* event) {
	deque<Event*> eventVec;
	eventVec.push_back(event);
	schedule_events_queue(eventVec);
}

void IOScheduler::finish_all_events_until_this_time(double time) {
	update_current_events();
	while ( get_current_time() < time && current_events.size() > 0 ) {
		execute_current_waiting_ios();
		update_current_events();
	}
}


void IOScheduler::execute_soonest_events() {
	finish_all_events_until_this_time(get_current_time() + 1);
}

// this is used to signal the SSD object when all events have finished executing
bool IOScheduler::is_empty() {
	return current_events.size() > 0 || future_events.size() > 0;
}

vector<Event*> IOScheduler::collect_soonest_events() {
	double current_time = get_current_time();
	vector<Event*> soonest_events;
	for (uint i = 0; i < current_events.size(); i++) {
		Event *e = current_events[i];
		if (e->get_current_time() < current_time + 1) {
			soonest_events.push_back(e);
			current_events.erase(current_events.begin() + i--);
		}
	}
	return soonest_events;
}

// tries to execute all current events. Some events may be put back in the queue if they cannot be executed.
void IOScheduler::execute_current_waiting_ios() {
	vector<Event*> events = collect_soonest_events();
	vector<Event*> read_commands;
	vector<Event*> read_transfers;
	vector<Event*> gc_writes;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> copy_backs;
	vector<Event*> noop_events;
	while (events.size() > 0) {
		Event * event = events.back();

		events.pop_back();
		event_type type = event->get_event_type();
		if (event->get_noop()) {
			noop_events.push_back(event);
		}
		else if (type == READ_COMMAND) {
			read_commands.push_back(event);
		}
		else if (type == READ_TRANSFER) {
			read_transfers.push_back(event);
		}
		else if (type == WRITE && !event->is_garbage_collection_op()) {
			writes.push_back(event);
		}
		else if (type == WRITE && event->is_garbage_collection_op()) {
			gc_writes.push_back(event);
		}
		else if (type == ERASE) {
			erases.push_back(event);
		}
		else if (type == COPY_BACK) {
			copy_backs.push_back(event);
		}
		else if (type == TRIM) {
			execute_next(event);
		}
	}
	handle_noop_events(noop_events);
	handle_next_batch(copy_backs); // Copy backs should be prioritized first to avoid conflict, since they have a reserved page waiting to be written
	handle_next_batch(erases);
	handle_next_batch(read_commands);
	handle_next_batch(read_transfers);
	handle_writes(gc_writes);
	handle_writes(writes);
}

double get_soonest_event_time(vector<Event*> events) {
	double earliest_time = events.front()->get_current_time();
	for (uint i = 1; i < events.size(); i++) {
		if (events[i]->get_current_time() < earliest_time) {
			earliest_time = events[i]->get_current_time();
		}
	}
	return earliest_time;
}

double IOScheduler::get_current_time() const {
	if (current_events.size() > 0) {
		return floor(get_soonest_event_time(current_events));
	}
	if (future_events.size() == 0) {
		return 0;
	}
	return floor(get_soonest_event_time(future_events));
}



// goes through all the events that has just been submitted (i.e. bus_wait_time = 0)
// in light of these new events, see if any other existing pending events are now redundant
void IOScheduler::update_current_events() {
	double current_time = get_current_time();
	for (uint i = 0; i < future_events.size(); i++) {
		Event* e = future_events[i];
	    if (e->get_current_time() < current_time + 1) {
	    	init_event(e);
	    	future_events.erase(future_events.begin() + i--);
	    }
	}
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
void IOScheduler::handle_writes(vector<Event*>& events) {
	sort(events.begin(), events.end(), bus_wait_time_comparator);
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		Address result = bm->choose_address(*event);
		double wait_time = bm->in_how_long_can_this_event_be_scheduled(result, event->get_current_time());
		if (wait_time == 0 && bm->Copy_backs_in_progress(result)) wait_time = 1; // If copy backs are in progress, keep waiting until they are done
		if (wait_time == 0) {
			ftl.set_replace_address(*event); // 05-09-2012: Moved to here from init_event (else if (type == WRITE) { ...)
			event->set_address(result);
			execute_next(event);
		}
		else {
			event->incr_bus_wait_time(wait_time);
			event->incr_time_taken(wait_time);
			current_events.push_back(event);
		}
	}
}

bool IOScheduler::should_event_be_scheduled(Event* event) {
	remove_redundant_events(event);
	uint la = event->get_logical_address();
	return LBA_currently_executing.count(la) == 1 && LBA_currently_executing[la] == event->get_application_io_id();
}

bool IOScheduler::remove_event_from_current_events(Event* event) {
	vector<Event*>::iterator e = std::find(current_events.begin(), current_events.end(), event);
	if (e == current_events.end()) return false;
	current_events.erase(e);
	return true;
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
	Event * existing_event = find_scheduled_event(dependency_code_of_other_event);

	//bool both_events_are_gc = new_event->is_garbage_collection_op() && existing_event->is_garbage_collection_op();
	//assert(!both_events_are_gc);

	event_type new_op_code = dependency_code_to_type[dependency_code_of_new_event];
	event_type scheduled_op_code = dependency_code_to_type[dependency_code_of_other_event];

	assert (existing_event != NULL || scheduled_op_code == COPY_BACK);

	// if something is to be trimmed, and a copy back is sent, the copy back is unnecessary to perform;
	// however, since the copy back destination address is already reserved, we need to use it.
	if (scheduled_op_code == COPY_BACK) {
		make_dependent(new_event, dependency_code_of_other_event);
	}
	else if (new_op_code == COPY_BACK) {
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		make_dependent(existing_event, new_event);
		remove_event_from_current_events(existing_event); // Remove old event from current_events; it's added again when independent event (the copy back) finishes
	}
	else if (new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		promote_to_gc(existing_event);
		remove_current_operation(new_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (existing_event->is_garbage_collection_op() && new_op_code == WRITE) {
		promote_to_gc(new_event);
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if two writes are scheduled, the one before is irrelevant and may as well be cancelled
	else if (new_op_code == WRITE && scheduled_op_code == WRITE) {
		remove_current_operation(existing_event);
		//existing_event->set_noop(true);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		stats.num_write_cancellations++;
	}
	else if (new_op_code == WRITE && scheduled_op_code == READ && existing_event->is_mapping_op()) {
		remove_current_operation(existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if there is a write, but before a read was scheduled, we should read first before making the write
	else if (new_op_code == WRITE && scheduled_op_code == READ) {
		assert(false);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if there is a read, and a write is scheduled, then the contents of the write must be buffered, so the read can wait
	else if (new_op_code == READ && scheduled_op_code == WRITE) {
		remove_current_operation(new_event);
	}
	// if there are two reads to the same address, there is no point reading the same page twice.
	else if (new_op_code == READ && scheduled_op_code == READ) {
		assert(false);
		make_dependent(new_event, existing_event);
		new_event->set_noop(true);
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
		assert(false);
		//make_dependent(new_event, new_op_code, scheduled_op_code);
	}
	// if a read is scheduled when a trim is received, we must still execute the read. Then we can trim
	else if (new_op_code == TRIM && (scheduled_op_code == READ || scheduled_op_code == READ_TRANSFER || scheduled_op_code == READ_COMMAND)) {
		make_dependent(new_event, existing_event);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if something is to be trimmed, and a read is sent, invalidate the read
	else if ((new_op_code == READ || new_op_code == READ_TRANSFER || new_op_code == READ_COMMAND) && scheduled_op_code == TRIM) {
		remove_current_operation(new_event);
		//new_event->set_noop(true);
	}
}

Event* IOScheduler::find_scheduled_event(uint dependency_code) {
	for (int i = current_events.size() - 1; i >= 0; i--) {
		Event * event = current_events[i];
		if (event->get_application_io_id() == dependency_code) {
			return event;
		}
	}
	//assert(false);
	return NULL;
}

void IOScheduler::remove_current_operation(Event* event) {
	event->set_noop(true);
	if (event->get_event_type() == READ_TRANSFER) {
		ssd.getPackages()[event->get_address().package].getDies()[event->get_address().die].clear_register();
	}
}

void IOScheduler::handle_noop_events(vector<Event*>& events) {
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		uint dependency_code = event->get_application_io_id();
		deque<Event*>& dependents = dependencies[dependency_code];
		ssd.register_event_completion(event);
		while (dependents.size() > 0) {
			Event *e = dependents.front();
			dependents.pop_front();
			ssd.register_event_completion(e);
		}
		dependencies.erase(dependency_code);
		dependency_code_to_LBA.erase(dependency_code);
		dependency_code_to_type.erase(dependency_code);
	}
}

void IOScheduler::promote_to_gc(Event* event_to_promote) {
	event_to_promote->set_garbage_collection_op(true);
	deque<Event*>& dependents = dependencies[event_to_promote->get_application_io_id()];
	for (uint i = 0; i < dependents.size(); i++){
		dependents[i]->set_garbage_collection_op(true);
	}
}

void IOScheduler::nullify_and_add_as_dependent(uint dependency_code_to_be_nullified, uint dependency_code_to_remain) {

}

void IOScheduler::make_dependent(Event* dependent_event, Event* independent_event) {
	make_dependent(dependent_event, independent_event->get_application_io_id());
}

void IOScheduler::make_dependent(Event* dependent_event, uint independent_code/*Event* independent_event_application_io*/) {
	uint dependent_code = dependent_event->get_application_io_id();
	op_code_to_dependent_op_codes[independent_code].push(dependent_code);
	dependencies[dependent_code].push_front(dependent_event);
}

// executes read_commands, read_transfers and erases
void IOScheduler::handle_next_batch(vector<Event*>& events) {
	sort(events.begin(), events.end(), bus_wait_time_comparator);
	for(uint i = 0; i < events.size(); i++) {
		Event* event = events[i];

		double time = bm->in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
		bool can_schedule = can_schedule_on_die(event);
		if (can_schedule && time == 0) {
			execute_next(event);
		}
		else {
			double bus_wait_time = can_schedule ? time : 1;
			event->incr_bus_wait_time(bus_wait_time);
			event->incr_time_taken(bus_wait_time);
			current_events.push_back(event);
		}
	}
}

enum status IOScheduler::execute_next(Event* event) {
	enum status result = ssd.controller.issue(event);

	if (PRINT_LEVEL > 0) {
		event->print();
	}

	if (result == SUCCESS) {
		int dependency_code = event->get_application_io_id();
		if (dependencies[dependency_code].size() > 0) {
			Event* dependent = dependencies[dependency_code].front();
			LBA_currently_executing.erase(event->get_logical_address());
			//LBA_currently_executing[dependent->get_logical_address()] = dependent->get_application_io_id();
			dependent->set_application_io_id(dependency_code);
			dependent->set_start_time(event->get_current_time());
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
			if (event->get_event_type() != ERASE) {
				if (LBA_currently_executing.count(lba) == 0) {
					printf("Assertion failure LBA_currently_executing.count(lba = %d) = %d, concerning ", lba, LBA_currently_executing.count(lba));
					event->print();
				}
				assert(LBA_currently_executing.count(lba) == 1);
				LBA_currently_executing.erase(lba);
				assert(LBA_currently_executing.count(lba) == 0);
				assert(dependency_code_to_LBA.count(dependency_code) == 1);
			}
			dependency_code_to_LBA.erase(dependency_code);
			dependency_code_to_type.erase(dependency_code);
			while (op_code_to_dependent_op_codes.count(dependency_code) == 1 && op_code_to_dependent_op_codes[dependency_code].size() > 0) {
				uint dependent_code = op_code_to_dependent_op_codes[dependency_code].front();
				op_code_to_dependent_op_codes[dependency_code].pop();
				Event* dependant_event = dependencies[dependent_code].front();
				if (dependant_event->get_event_type() == TRIM) {
					dependant_event->print();
				}
				dependencies[dependent_code].pop_front();
				future_events.push_back(dependant_event);
			}
			op_code_to_dependent_op_codes.erase(dependency_code);
		}
	} else {
		fprintf(stderr, "execute_event: Event failed! ");
		dependencies.erase(event->get_application_io_id()); // possible memory leak here, since events are not deleted
	}

	handle_finished_event(event, result);
	return result;
}

bool IOScheduler::can_schedule_on_die(Event const* event) const {
	uint package_id = event->get_address().package;
	uint die_id = event->get_address().die;
	bool busy = ssd.getPackages()[package_id].getDies()[die_id].register_is_busy();
	if (!busy) {
		return true;
	}
	uint application_io = ssd.getPackages()[package_id].getDies()[die_id].get_last_read_application_io();
	return event->get_event_type() == READ_TRANSFER && application_io == event->get_application_io_id();
}

void IOScheduler::handle_finished_event(Event *event, enum status outcome) {
	if (outcome == FAILURE) {
		assert(false); // events should not fail at this point. Any failure indicates application error
	}
	VisualTracer::get_instance()->register_completed_event(*event);
	if (event->get_event_type() == WRITE || event->get_event_type() == COPY_BACK) {
		ftl.register_write_completion(*event, outcome);
		bm->register_write_outcome(*event, outcome);
		//StateTracer::print();
	} else if (event->get_event_type() == ERASE) {
		bm->register_erase_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_COMMAND) {
		bm->register_read_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_TRANSFER) {
		ftl.register_read_completion(*event, outcome);
	} else if (event->get_event_type() == TRIM) {
		ftl.register_trim_completion(*event);
		bm->trim(*event);
	}
	StatisticsGatherer::get_instance()->register_completed_event(*event);

	ssd.register_event_completion(event);
}

void IOScheduler::print_stats() {
	printf("\n");
	printf("num_write_cancellations %d\n", stats.num_write_cancellations);
	printf("\n");
}

void IOScheduler::init_event(Event* event) {
	uint dep_code = event->get_application_io_id();
	event_type type = event->get_event_type();

	if (type == TRIM || type == READ_COMMAND || type == READ_TRANSFER || type == WRITE || type == COPY_BACK) {
		if (should_event_be_scheduled(event)) {
			current_events.push_back(event);
		} else {
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
	else if (type == READ_COMMAND || type == READ_TRANSFER) {
		ftl.set_read_address(*event);
	}
	else if (type == WRITE) {
		bm->register_write_arrival(*event);
//		ftl.set_replace_address(*event);
	}
	else if (type == TRIM) {
		ftl.set_replace_address(*event);
	}
	else if (type == GARBAGE_COLLECTION) {
		vector<deque<Event*> > migrations = bm->migrate(event);
		while (migrations.size() > 0) {
			// Pick first migration from deque
			deque<Event*> migration = migrations.back();
			migrations.pop_back();
			// Pick first event from migration
			Event* first = migration.front();

			if (first->get_event_type() == COPY_BACK) {
				// Make a chain of dependencies, to enforce an order of execution: first -> second -> third, etc.
				for (deque<Event*>::iterator e = migration.begin(); e != migration.end(); e++) {
					Event* current = *(e);
					Event* next    = *(e+1);
					bool last  = (e+1 == migration.end());
					bool first = (e   == migration.begin());
					dependency_code_to_LBA[current->get_application_io_id()] = current->get_logical_address();
					dependency_code_to_type[current->get_application_io_id()] = current->get_event_type(); // = WRITE for normal GC, COPY_BACK for copy backs
					if (first) dependencies[current->get_application_io_id()] = deque<Event*>();
					remove_redundant_events(current);
					if (!last) make_dependent(next, current);
				}
			} else {
				migration.pop_front();
				dependencies[first->get_application_io_id()] = migration;
				dependency_code_to_LBA[first->get_application_io_id()] = first->get_logical_address();
				dependency_code_to_type[first->get_application_io_id()] = first->get_event_type(); // = WRITE for normal GC, COPY_BACK for copy backs
			}
			init_event(first);
		}
		delete event;
	}
	else if (type == ERASE) {
		current_events.push_back(event);
	}
}
