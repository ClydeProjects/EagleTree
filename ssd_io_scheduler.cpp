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

bool myfunction (const Event* i, const Event* j) {
	return i->get_current_time() > j->get_current_time();
}

bool bus_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
}

void IOScheduler::schedule_dependent_events(std::queue<Event*>& events, ulong logical_address, event_type type) {
	uint dependency_code = events.back()->get_application_io_id();
	if (dependency_code == 23) {
		int i = 0;
		i++;
	}
	dependency_code_to_LBA[dependency_code] = logical_address;
	dependency_code_to_type[dependency_code] = type;
	assert(dependencies.count(dependency_code) == 0);
	while (!events.empty()) {
		Event* event = events.front();
		events.pop();
		event->set_application_io_id(dependency_code);
		if (event->get_event_type() == READ) {
			event->set_event_type(READ_TRANSFER);
			Event* read_command = new Event(READ_COMMAND, event->get_logical_address(), event->get_size(), event->get_start_time());
			read_command->set_application_io_id(dependency_code);
			read_command->set_garbage_collection_op(event->is_garbage_collection_op());
			read_command->set_mapping_op(event->is_mapping_op());
			dependencies[dependency_code].push_back(read_command);
		}
		/*if (event->is_garbage_collection_op() && event->get_event_type() == WRITE) {
			assert(LBA_to_dependencies.count(event->get_logical_address()) == 0);
			LBA_to_dependencies[event->get_logical_address()] = dependency_code;
		}*/
		dependencies[dependency_code].push_back(event);
	}
	Event* first = dependencies[dependency_code].front();
	dependencies[dependency_code].pop_front();

	io_schedule.push_back(first);
	//std::sort(io_schedule.begin(), io_schedule.end(), myfunction);

	//printf("%d  %f\nSTART\n", first->get_logical_address(), first->get_start_time());

	for (int i = 0; i < io_schedule.size(); i++) {
		//printf("%d  %f\n", io_schedule[i]->get_logical_address(), io_schedule[i]->get_start_time());
	}
	//printf("\n");
	//std::sort(io_schedule.begin(), io_schedule.end(), myfunction);
	Event* e = io_schedule.back();
	while (io_schedule.size() > 0 && io_schedule.back()->get_current_time() + 1 <= first->get_start_time()) {
		execute_current_waiting_ios();
	}
	assert(events.empty());
}

void IOScheduler::schedule_independent_event(Event* event, ulong logical_address, event_type type) {
	std::queue<Event*> eventVec;
	eventVec.push(event);
	schedule_dependent_events(eventVec, logical_address, type);
}

void IOScheduler::finish(double start_time) {
	std::sort(io_schedule.begin(), io_schedule.end(), myfunction);
	while (io_schedule.size() > 0 && io_schedule.back()->get_current_time() + 1 < start_time) {
		execute_current_waiting_ios();
	}
}

void IOScheduler::progess() {
	if (io_schedule.size() > 0) {
		execute_current_waiting_ios();
	}
}

// takes the pending events from io_schedule whose start_time + wait_time is soonest, are that are all within 1 microsecond of eachother.
// divides them into structures based on type. Gives piorities to different types of operations
void IOScheduler::execute_current_waiting_ios() {
	assert(io_schedule.size() > 0);
	std::sort(io_schedule.begin(), io_schedule.end(), myfunction);
	vector<Event*> current_ios = test_for_removing_reduntant_events();
	vector<Event*> read_commands;
	vector<Event*> read_transfers;
	vector<Event*> gc_writes;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> trims;
	while (current_ios.size() > 0) {
		Event * event = current_ios.back();
		current_ios.pop_back();
		event_type type = event->get_event_type();
		if (type == READ_COMMAND) {
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


// goes through all the events that has just been submitted (i.e. bus_wait_time = 0)
// in light of these new events, see if any other existing pending events are now redundant
vector<Event*> IOScheduler::test_for_removing_reduntant_events() {
	double const start_time = io_schedule.back()->get_current_time();
	double time = start_time;
	int i = io_schedule.size() - 1;
	for (; i >= 0; i--) {
		Event * event = io_schedule[i];
		time = event->get_current_time();
		if (time >= start_time + 1) {
			break;
		}
		if (event->get_bus_wait_time() == 0) {
			uint la = event->get_logical_address();
			if (LBA_currently_executing.count(la) == 1 && LBA_currently_executing[la] != event->get_application_io_id()) {
				remove_redundant_events(i);
			} else if (LBA_currently_executing.count(la) == 0 && event->get_event_type() != ERASE) {
				LBA_currently_executing[event->get_logical_address()] = event->get_application_io_id();
			}
		}
	}
	int num_current_events = io_schedule.size() - 1 - i;
	if (num_current_events == 2) {
		int i = 0;
		i++;
	}
	vector<Event*> current_ios;
	for (int j = 0; j < num_current_events; j++) {
		Event * event = io_schedule.back();
		io_schedule.pop_back();
		current_ios.push_back(event);
	}
	return current_ios;
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
void IOScheduler::handle_writes(std::vector<Event*>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		if (event->get_bus_wait_time() == 0) {
			bm->register_write_arrival(*event);
		}
		assert(event->get_event_type() == WRITE);
		ftl.set_replace_address(*event);
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
			io_schedule.push_back(event);
		}
	}
}


void IOScheduler::remove_redundant_events(int index) {
	Event * new_event = io_schedule[index];
	if (new_event->get_event_type() == ERASE) {
		return;
	}
	uint dependency_code_of_new_event = new_event->get_application_io_id();
	uint common_logical_address = new_event->get_logical_address();
	uint dependency_code_of_other_event = LBA_currently_executing[common_logical_address];
	int index_of_other = find_scheduled_event(dependency_code_of_other_event);
	Event * currently_scheduled_event = io_schedule[index_of_other];
	assert(currently_scheduled_event != NULL);
	bool both_events_are_gc = new_event->is_garbage_collection_op() && currently_scheduled_event->is_garbage_collection_op();
	assert(!both_events_are_gc);

	event_type new_op_code = dependency_code_to_type[dependency_code_of_new_event];
	event_type scheduled_op_code = dependency_code_to_type[dependency_code_of_other_event];

	if (new_event->is_garbage_collection_op() && scheduled_op_code == WRITE) {
		promote_to_gc(index_of_other);
		remove_operation(index);
		LBA_currently_executing[common_logical_address] = dependency_code_of_other_event;
	}
	else if (currently_scheduled_event->is_garbage_collection_op() && new_op_code == WRITE) {
		promote_to_gc(index);
		remove_operation(index_of_other);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
	}
	// if two writes are scheduled, the one before is irrelevant and may as well be cancelled
	else if (new_op_code == WRITE && scheduled_op_code == WRITE) {
		remove_operation(index_of_other);
		LBA_currently_executing[common_logical_address] = dependency_code_of_new_event;
		stats.num_write_cancellations++;
	}
	// if there is a write, but before a read was scheduled, we should read first before making the write
	else if (new_op_code == WRITE && scheduled_op_code == READ) {
		make_dependent(new_event, new_op_code, scheduled_op_code);
	}
	// if there is a read, and a write is scheduled, then the contents of the write must be buffered, so the read can wait
	else if (new_op_code == READ && scheduled_op_code == WRITE) {
		remove_operation(index);
	}
	// if there are two reads to the same address, there is no point reading the same page twice.
	else if (new_op_code == READ && scheduled_op_code == READ) {
		assert(false);
		nullify_and_add_as_dependent(new_op_code, scheduled_op_code);
	}
	// if a write is scheduled when a trim is received, we may as well cancel the write
	else if (new_op_code == TRIM && scheduled_op_code == WRITE) {
		remove_operation(index);
	}
	// if a trim is scheduled, and a write arrives, may as well let the trim execute first
	else if (new_op_code == WRITE && scheduled_op_code == TRIM) {
		make_dependent(new_event, new_op_code, scheduled_op_code);
	}
	// if a read is scheduled when a trim is received, we must still execute the read. Then we can trim
	else if (new_op_code == TRIM && scheduled_op_code == READ) {
		make_dependent(new_event, new_op_code, scheduled_op_code);
	}
	// if something is to be trimmed, and a read is sent, invalidate the read
	else if (new_op_code == READ && scheduled_op_code == TRIM) {
		remove_operation(index);
	}


}

int IOScheduler::find_scheduled_event(uint dependency_code) const {
	for (int i = io_schedule.size() - 1; i >= 0; i--) {
		Event * event = io_schedule[i];
		if (event->get_application_io_id() == dependency_code) {
			return i;
		}
	}
	return -1;
}

void IOScheduler::remove_operation(uint index_of_event_in_io_schedule) {
	Event * event = io_schedule[index_of_event_in_io_schedule];
	uint dependency_code = event->get_application_io_id();
	deque<Event*>& dependents = dependencies[dependency_code];
	if (event->get_event_type() == READ_TRANSFER) {
		ssd.getPackages()[event->get_address().package].getDies()[event->get_address().die].clear_register();
	}
	ssd.register_event_completion(event);
	while (dependents.size() > 0) {
		Event *e = dependents.front();
		dependents.pop_front();
		ssd.register_event_completion(e);
	}
	dependencies.erase(dependency_code);
	io_schedule.erase(io_schedule.begin() + index_of_event_in_io_schedule);
}

void IOScheduler::promote_to_gc(uint index_of_event_in_io_schedule) {
	Event * event = io_schedule[index_of_event_in_io_schedule];
	event->set_garbage_collection_op(true);
	deque<Event*>& dependents = dependencies[event->get_application_io_id()];
	for (int i = 0; i < dependents.size(); i++){
		dependents[i]->set_garbage_collection_op(true);
	}
}

void IOScheduler::nullify_and_add_as_dependent(uint dependency_code_to_be_nullified, uint dependency_code_to_remain) {

}

void IOScheduler::make_dependent(Event * new_event, uint op_code_to_be_made_dependent, uint op_code_to_remain) {
	op_code_to_dependent_op_codes[op_code_to_remain] = vector<uint>(0);
	op_code_to_dependent_op_codes[op_code_to_remain].push_back(op_code_to_be_made_dependent);
	dependencies[op_code_to_be_made_dependent].push_front(new_event);
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
/*void IOScheduler::handle_writes(std::vector<Event*>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	//printf("STARTTTTTTTTTTTTTTT\n");
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		if (event->get_bus_wait_time() == 0) {
			bm->register_write_arrival(*event);
		}
		assert(event->get_event_type() == WRITE);
		ftl.set_replace_address(*event);
		//bm.register_write_arrival(event);
		if (!event->is_garbage_collection_op()) {
			eliminate_conflict_with_any_incoming_gc(event);
		}
		pair<double, Address> result = bm->write(*event);
		if (result.first == 0) {
			event->set_address(result.second);
			event->set_noop(false);
			ftl.set_replace_address(*event);
			adjust_conflict_elimination_structures(event);
			execute_next(event);
		}
		else {
			event->incr_bus_wait_time(result.first);
			event->incr_time_taken(result.first);
			io_schedule.push_back(event);
		}
	}
}*/

// if a write to LBA X arrives, while there is already a pending GC operation
// to migrate LBA X, the GC operation becomes redundant, so we cancel it.
/*void IOScheduler::eliminate_conflict_with_any_incoming_gc(Event * event) {
	assert(!event->is_garbage_collection_op());
	//if (LBA_to_dependencies.count(event->get_logical_address()) == 0) {
		return;
	}
	uint dependency_code = LBA_to_dependencies[event->get_logical_address()];
	deque<Event*> gc_queue = dependencies[dependency_code];

	Address addr_of_original_page;
	uint num_events_eliminated = 0;
	for (uint i = 0; i < gc_queue.size(); i++) {
		Event* gc_event = gc_queue[i];
		if (gc_event->get_logical_address() == event->get_logical_address()) {
			gc_queue.erase (gc_queue.begin()+i);
			i--;
			num_events_eliminated++;
			if (addr_of_original_page.valid == NONE) {
				if (gc_event->get_event_type() == READ_COMMAND || gc_event->get_event_type() == READ_TRANSFER) {
					addr_of_original_page = gc_event->get_address();
				} else if (gc_event->get_event_type() == WRITE) {
					addr_of_original_page = gc_event->get_replace_address();
				}
			}
		}
	}

	if (num_events_eliminated < 3) {
		for (uint i = 0; i < io_schedule.size(); i++) {
			Event* some_event = io_schedule[i];
			if (some_event->is_garbage_collection_op() && some_event->get_logical_address() == event->get_logical_address()) {
				io_schedule.erase(io_schedule.begin() + i);
				i--;
				num_events_eliminated++;
				if (addr_of_original_page.valid == NONE) {
					if (some_event->get_event_type() == READ_COMMAND || some_event->get_event_type() == READ_TRANSFER) {
						addr_of_original_page = some_event->get_address();
					} else if (some_event->get_event_type() == WRITE) {
						addr_of_original_page = some_event->get_replace_address();
					}
				}
				break;
			}
		}
	}
	if (num_events_eliminated == 2) {
		ssd.getPackages()[addr_of_original_page.package].getDies()[addr_of_original_page.die].clear_register();
	}
	if (PRINT_LEVEL > 1) {
		printf("Write makes pending GC unnecessary. LBA: %d   removed %d events", event->get_logical_address(), num_events_eliminated);
		event->get_replace_address().print();
		printf("\n");
	}

	event->set_garbage_collection_op(true);
	if (num_events_eliminated == 0) {
		int i = 0;
		i++;
		event->print();
	}
	assert(num_events_eliminated > 0);

	//LBA_to_dependencies.erase(event->get_logical_address());
}

// invoked upon completion of a write
void IOScheduler::adjust_conflict_elimination_structures(Event const*const event) {
	if (event->is_garbage_collection_op()) {
		//LBA_to_dependencies.erase(event->get_logical_address());
	}
}*/

// executes read_commands, read_transfers and erases
void IOScheduler::execute_next_batch(std::vector<Event*>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	for(uint i = 0; i < events.size(); i++) {
		Event* event = events[i];
		assert(event->get_event_type() != WRITE);
		if (event->get_event_type() == READ_COMMAND || event->get_event_type() == READ_TRANSFER) {
			ftl.set_read_address(*event);
		}
		double time = bm->in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
		bool can_schedule = can_schedule_on_die(event);
		if (can_schedule && time <= 0) {
			execute_next(event);
		}
		else {
			double bus_wait_time;
			if (time > 0) {
				bus_wait_time = time;
			} else {
				bus_wait_time = 1;
			}
			event->incr_bus_wait_time(bus_wait_time);
			event->incr_time_taken(bus_wait_time);
			io_schedule.push_back(event);
		}
	}
}

enum status IOScheduler::execute_next(Event* event) {
	enum status result = ssd.controller.issue(*event);
	if (result == SUCCESS) {
		int dependency_code = event->get_application_io_id();
		if (dependencies[dependency_code].size() > 0) {
			Event* dependent = dependencies[dependency_code].front();
			dependent->set_start_time(event->get_current_time());
			dependencies[dependency_code].pop_front();
			io_schedule.push_back(dependent);
			if (event->get_event_type() == READ_COMMAND && dependent->get_event_type() == READ_TRANSFER) {
				dependent->set_address(event->get_address());
			}
		} else {
			assert(dependencies.count(dependency_code) == 1);
			dependencies.erase(dependency_code);
			assert(dependency_code_to_LBA.count(dependency_code) == 1);
			uint lba = dependency_code_to_LBA[dependency_code];
			dependency_code_to_LBA.erase(dependency_code);
			if (event->get_event_type() != ERASE) {
				if (LBA_currently_executing.count(lba) != 1) {
					int i = 0;
					event->print();
					i++;
				}
				assert(LBA_currently_executing.count(lba) == 1);
				LBA_currently_executing.erase(lba);
			}
			dependency_code_to_type.erase(dependency_code);
		}
	} else {
		printf("FAILED!!!! ");
		dependencies.erase(event->get_application_io_id()); // possible memory leak here, since events are not deleted
	}

	/*double io_start_time = event->get_start_time() + event->get_bus_wait_time();
	assert(io_start_time >=  time_of_last_IO_execution_start);
	time_of_last_IO_execution_start = io_start_time;*/

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
	}
	StatisticsGatherer::get_instance()->register_completed_event(*event);

	ssd.register_event_completion(event);
}

void IOScheduler::print_stats() {
	printf("\n");
	printf("num_write_cancellations %d\n", stats.num_write_cancellations);
	printf("\n");
}
