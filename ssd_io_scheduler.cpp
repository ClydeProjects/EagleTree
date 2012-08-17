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
		bm = new Block_manager_parallel_wearwolf(ssd, ftl);
	} else if (BLOCK_MANAGER_ID == 3) {
		bm = new Block_manager_parallel_wearwolf_locality(ssd, ftl);
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

bool current_time_comparator (const Event* i, const Event* j) {
	return i->get_current_time() > j->get_current_time();
}

bool bus_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
}

// TODO pass a deque dependencies instead of converting from a queue
void IOScheduler::schedule_dependent_events(deque<Event*> events, ulong logical_address, event_type type) {
	uint dependency_code = events.front()->get_application_io_id();
	if (type != GARBAGE_COLLECTION && type != ERASE) {
		dependency_code_to_LBA[dependency_code] = logical_address;
	}
	dependency_code_to_type[dependency_code] = type;
	assert(dependencies.count(dependency_code) == 0);
	dependencies[dependency_code] = events;
	Event* first = dependencies[dependency_code].front();
	dependencies[dependency_code].pop_front();
	future_events.push_back(first);
	printf("received : "); first->print();
}

void IOScheduler::schedule_independent_event(Event* event, ulong logical_address, event_type type) {
	deque<Event*> eventVec;
	eventVec.push_back(event);
	schedule_dependent_events(eventVec, logical_address, type);
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

bool IOScheduler::is_empty() {
	return current_events.size() > 0 || future_events.size() > 0;
}

// takes the pending events from io_schedule whose start_time + wait_time is soonest, are that are all within 1 microsecond of eachother.
// divides them into structures based on type. Gives piorities to different types of operations
void IOScheduler::execute_current_waiting_ios() {
	vector<Event*> read_commands;
	vector<Event*> read_transfers;
	vector<Event*> gc_writes;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> trims;
	while (current_events.size() > 0) {
		Event * event = current_events.back();
		current_events.pop_back();
		event_type type = event->get_event_type();
		if (event->get_noop()) {
			execute_next(event);
		} else 	if (type == GARBAGE_COLLECTION) {
			delete event;
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
		else if (type == TRIM) {
			ftl.set_replace_address(*event);
			handle_finished_event(event, SUCCESS);
		}
	}
	//printf("\n -------------------------------- \n");
	execute_next_batch(erases);
	execute_next_batch(read_commands);
	execute_next_batch(read_transfers);
	handle_writes(gc_writes);
	handle_writes(writes);
}

double IOScheduler::get_current_time() const {
	if (current_events.size() > 0) {
		return floor(current_events.back()->get_current_time());
	}
	if (future_events.size() == 0) {
		return 0;
	}
	uint earliest_event = 0;
	double earliest_time = future_events[0]->get_start_time();
	for (uint i = 1; i < future_events.size(); i++) {
		if (future_events[i]->get_start_time() < earliest_event) {
			earliest_event = i;
			earliest_time = future_events[i]->get_start_time();
		}
	}
	return floor(earliest_time);
}


// goes through all the events that has just been submitted (i.e. bus_wait_time = 0)
// in light of these new events, see if any other existing pending events are now redundant
void IOScheduler::update_current_events() {
	double current_time = get_current_time();
	for (uint i = 0; i < future_events.size(); i++) {
	    if (future_events[i]->get_start_time() < current_time + 1) {
	    	init_event(future_events[i]);
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
		pair<double, Address> result = bm->write(*event);
		if (result.first == 0) {
			event->set_address(result.second);
			event->set_noop(false);
			ftl.set_replace_address(*event);
			execute_next(event);
		}
		else {
			event->incr_bus_wait_time(result.first);
			event->incr_time_taken(result.first);
			current_events.push_back(event);
		}
	}
}


void IOScheduler::remove_redundant_events(Event* new_event) {
	if (new_event->get_event_type() == ERASE || new_event->get_event_type() == GARBAGE_COLLECTION) {
		return;
	}
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
	int index_of_existing_event = find_scheduled_event(dependency_code_of_other_event);
	int index_of_new_event = find_scheduled_event(dependency_code_of_new_event);
	Event * current_event = current_events[index_of_existing_event];
	assert(current_event != NULL);
	bool both_events_are_gc = new_event->is_garbage_collection_op() && current_event->is_garbage_collection_op();

	assert(!both_events_are_gc);

	event_type new_op_code = dependency_code_to_type[dependency_code_of_new_event];
	event_type scheduled_op_code = dependency_code_to_type[dependency_code_of_other_event];

	if (new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		promote_to_gc(current_event);
		remove_current_operation(index_of_new_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (current_event->is_garbage_collection_op() && new_op_code == WRITE) {
		promote_to_gc(new_event);
		remove_current_operation(index_of_existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if two writes are scheduled, the one before is irrelevant and may as well be cancelled
	else if (new_op_code == WRITE && scheduled_op_code == WRITE) {
		remove_current_operation(index_of_existing_event);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		stats.num_write_cancellations++;
	}
	// if there is a write, but before a read was scheduled, we should read first before making the write
	else if (new_op_code == WRITE && scheduled_op_code == READ) {
		assert(false);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if there is a read, and a write is scheduled, then the contents of the write must be buffered, so the read can wait
	else if (new_op_code == READ && scheduled_op_code == WRITE) {
		remove_current_operation(index_of_new_event);
	}
	// if there are two reads to the same address, there is no point reading the same page twice.
	else if (new_op_code == READ && scheduled_op_code == READ) {
		assert(false);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
		new_event->set_noop(true);
	}
	// if a write is scheduled when a trim is received, we may as well cancel the write
	else if (new_op_code == TRIM && scheduled_op_code == WRITE) {
		remove_current_operation(index_of_new_event);
	}
	// if a trim is scheduled, and a write arrives, may as well let the trim execute first
	else if (new_op_code == WRITE && scheduled_op_code == TRIM) {
		assert(false);
		//make_dependent(new_event, new_op_code, scheduled_op_code);
	}
	// if a read is scheduled when a trim is received, we must still execute the read. Then we can trim
	else if (new_op_code == TRIM && scheduled_op_code == READ) {
		assert(false);
		//make_dependent(new_event, dependency_code_of_new_event, dependency_code_of_other_event);
	}
	// if something is to be trimmed, and a read is sent, invalidate the read
	else if (new_op_code == READ && scheduled_op_code == TRIM) {
		remove_current_operation(index_of_new_event);
	}


}

int IOScheduler::find_scheduled_event(uint dependency_code) const {
	for (int i = current_events.size() - 1; i >= 0; i--) {
		Event * event = current_events[i];
		if (event->get_application_io_id() == dependency_code) {
			return i;
		}
	}
	//assert(false);
	return -1;
}

void IOScheduler::remove_current_operation(uint index_of_event_in_io_schedule) {
	Event * event = current_events[index_of_event_in_io_schedule];
	current_events.erase(current_events.begin() + index_of_event_in_io_schedule);

	uint dependency_code = event->get_application_io_id();
	deque<Event*>& dependents = dependencies[dependency_code];
	ssd.register_event_completion(event);
	if (event->get_event_type() == READ_TRANSFER) {
		ssd.getPackages()[event->get_address().package].getDies()[event->get_address().die].clear_register();
	}
	while (dependents.size() > 0) {
		Event *e = dependents.front();
		dependents.pop_front();
		ssd.register_event_completion(e);
	}
	dependencies.erase(dependency_code);
	dependency_code_to_LBA.erase(dependency_code);
	dependency_code_to_type.erase(dependency_code);
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

void IOScheduler::make_dependent(Event* new_event, uint op_code_to_be_made_dependent, uint op_code_to_remain) {
	/*op_code_to_dependent_op_codes[op_code_to_remain].push(op_code_to_be_made_dependent);
	Event * event = future_events[new_event_index];
	dependencies[op_code_to_be_made_dependent].push_front(event);
	future_events.erase(future_events.begin() + new_event_index);*/
}

// executes read_commands, read_transfers and erases
void IOScheduler::execute_next_batch(vector<Event*>& events) {
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
	enum status result = event->get_noop() ? SUCCESS : ssd.controller.issue(*event);
	if (result == SUCCESS) {
		int dependency_code = event->get_application_io_id();
		if (dependencies[dependency_code].size() > 0) {
			Event* dependent = dependencies[dependency_code].front();
			dependent->set_application_io_id(dependency_code);
			dependent->set_start_time(event->get_current_time());
			dependent->set_noop(event->get_noop());
			dependencies[dependency_code].pop_front();
			current_events.push_back(dependent);
			if (event->get_event_type() == READ_COMMAND && dependent->get_event_type() == READ_TRANSFER) {
				dependent->set_address(event->get_address());
			}
		} else {
			assert(dependencies.count(dependency_code) == 1);
			dependencies.erase(dependency_code);
			uint lba = dependency_code_to_LBA[dependency_code];
			if (event->get_event_type() != ERASE) {
				if (LBA_currently_executing.count(lba) == 0) {
					int i = 0;
					i++;
					event->print();
					assert(false);
				}
				assert(LBA_currently_executing.count(lba) == 1);
				LBA_currently_executing.erase(lba);
				assert(dependency_code_to_LBA.count(dependency_code) == 1);
			}
			dependency_code_to_LBA.erase(dependency_code);
			dependency_code_to_type.erase(dependency_code);
			while (op_code_to_dependent_op_codes.count(dependency_code) == 1 && op_code_to_dependent_op_codes[dependency_code].size() > 0) {
				uint dependent_code = op_code_to_dependent_op_codes[dependency_code].front();
				op_code_to_dependent_op_codes[dependency_code].pop();
				Event* dependant_event = dependencies[dependent_code].front();
				dependencies[dependent_code].pop_front();
				current_events.push_back(dependant_event);
				init_event(dependant_event);
			}
			op_code_to_dependent_op_codes.erase(dependency_code);
		}
	} else {
		printf("FAILED!!!! ");
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
	if (PRINT_LEVEL > 0) {
		event->print();
	}
	if (outcome == FAILURE) {
		return;
	}

	VisualTracer::get_instance()->register_completed_event(*event);
	if (event->get_event_type() == WRITE) {
		ftl.register_write_completion(*event, outcome);
		bm->register_write_outcome(*event, outcome);
	} else if (event->get_event_type() == ERASE) {
		bm->register_erase_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_COMMAND) {
		bm->register_read_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_TRANSFER) {
		ftl.register_read_completion(*event, outcome);
	} else if (event->get_event_type() == TRIM) {
		ftl.register_trim_completion(*event);
		bm->trim(*event);
	} else {
		assert(false);
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
	if (event->get_event_type() == READ) {
		event->set_event_type(READ_COMMAND);

		// This (simple copy) ...
		Event* read_transfer = new Event(*event);
		read_transfer->set_event_type(READ_TRANSFER);

		// ... replaces this (new event, then copy each attribute):
		/*
		Event* read_transfer = new Event(READ_TRANSFER, event->get_logical_address(), event->get_size(), event->get_start_time());
		read_transfer->set_application_io_id(dep_code);
		read_transfer->set_garbage_collection_op(event->is_garbage_collection_op());
		read_transfer->set_mapping_op(event->is_mapping_op());
		read_transfer->set_original_application_io(event->is_original_application_io());
		*/

		dependencies[dep_code].push_front(read_transfer);
		ftl.set_read_address(*event);
		ftl.set_read_address(*read_transfer);
		current_events.push_back(event);
	}
	else if (event->get_event_type() == READ_COMMAND || event->get_event_type() == READ_TRANSFER) {
		ftl.set_read_address(*event);
		current_events.push_back(event);
	}
	else if (event->get_event_type() == WRITE) {
		bm->register_write_arrival(*event);
		ftl.set_replace_address(*event);
		current_events.push_back(event);
	}
	else if (event->get_event_type() == GARBAGE_COLLECTION) {
		vector<deque<Event*> > migrations = bm->migrate(event);
		while (migrations.size() > 0) {
			deque<Event*> migration = migrations.back();
			migrations.pop_back();
			Event * first = migration.front();
			migration.pop_front();
			dependencies[first->get_application_io_id()] = migration;
			dependency_code_to_LBA[first->get_application_io_id()] = first->get_logical_address();
			dependency_code_to_type[first->get_application_io_id()] = WRITE;
			init_event(first);
		}
		delete event;
	}
	else if (event->get_event_type() == ERASE) {
		current_events.push_back(event);
	}
	remove_redundant_events(event);
}
