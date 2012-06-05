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
	bm(ssd, ftl),
	pending_writes()
{}

IOScheduler::~IOScheduler(){}

IOScheduler *IOScheduler::inst = NULL;

void IOScheduler::instance_initialize(Ssd& ssd, FtlParent& ftl)
{
	IOScheduler::inst = new IOScheduler(ssd, ftl);
}

IOScheduler *IOScheduler::instance()
{
	return IOScheduler::inst;
}

bool myfunction (const Event& i, const Event& j) {
	return i.get_start_time() + i.get_time_taken() > j.get_start_time() + j.get_time_taken();
}

bool bus_wait_time_comparator (const Event& i, const Event& j) {
	return i.get_bus_wait_time() > j.get_bus_wait_time();
}

void IOScheduler::schedule_dependent_events(std::queue<Event>& events) {
	uint dependency_code = events.back().get_application_io_id();
	assert(dependencies.count(dependency_code) == 0);
	while (!events.empty()) {
		Event event = events.front();
		events.pop();
		event.set_application_io_id(dependency_code);
		if (event.is_garbage_collection_op() && pending_writes.count(event.get_logical_address()) == 1) {
			continue;
		}
		if (event.get_event_type() == READ) {
			event.set_event_type(READ_TRANSFER);
			Event* read_command = new Event(READ_COMMAND, event.get_logical_address(), event.get_size(), event.get_start_time());
			read_command->set_address(event.get_address());
			read_command->set_application_io_id(dependency_code);
			read_command->set_garbage_collection_op(event.is_garbage_collection_op());
			dependencies[dependency_code].push_back(*read_command);
		}
		if (event.is_garbage_collection_op() && event.get_event_type() == WRITE) {
			LBA_to_dependencies[event.get_logical_address()] = dependency_code;
		}
		dependencies[dependency_code].push_back(event);
	}
	Event first = dependencies[dependency_code].front();
	dependencies[dependency_code].pop_front();

	io_schedule.push_back(first);
	std::sort(io_schedule.begin(), io_schedule.end(), myfunction);

	while (io_schedule.back().get_start_time() + 1 <= first.get_start_time()) {
		execute_current_waiting_ios();
	}
	assert(events.empty());
}

void IOScheduler::schedule_independent_event(Event& event) {
	std::queue<Event> eventVec;
	eventVec.push(event);
	schedule_dependent_events(eventVec);
}

void IOScheduler::finish(double start_time) {
	while (!io_schedule.empty() && io_schedule.back().get_start_time() + 1 < start_time) {
		execute_current_waiting_ios();
	}
}

std::vector<Event> IOScheduler::gather_current_waiting_ios() {
	Event top = io_schedule.back();
	io_schedule.pop_back();
	Event top2 = io_schedule.back();
	double start_time = top.get_start_time() + top.get_time_taken();
	std::vector<Event> current_ios;
	current_ios.push_back(top);
	while (io_schedule.size() > 0 && start_time + 1 > io_schedule.back().get_start_time() + io_schedule.back().get_time_taken()) {
		Event current_top = io_schedule.back();
		io_schedule.pop_back();
		current_ios.push_back(current_top);
	}
	return current_ios;
}

/*void IOScheduler::consolidate_events(std::vector<Event>& events) {
	for (uint i = 0; i < events.size(); i++) {
		Event event = events[i];
		uint LBA = event.get_logical_address();
		if (LBA_to_dependencies.count(LBA) == 1) {
			std::deque<Event> deque = dependencies[event.get_application_io_id()];
			std::vector<uint> indicies_with_LBA(0);
			for (int j = 0; j < deque.size(); j++) {
				if (deque[j].get_logical_address() == LBA) {
					indicies_with_LBA.push_back(j);
				}
			}

		} else {
			LBA_to_dependencies[LBA] = event.get_application_io_id();
		}
	}
}

void IOScheduler::consolidate(Event& event, uint dependency_code) {
	std::deque<Event> deque = dependencies[dependency_code];
	if (event.get_event_type() == READ_COMMAND) {

	}
}*/

void IOScheduler::execute_current_waiting_ios() {
	assert(!io_schedule.empty());
	std::vector<Event> current_ios = gather_current_waiting_ios();
	std::vector<Event> read_commands;
	std::vector<Event> read_transfers;
	std::vector<Event> writes;
	std::vector<Event> erases;
	for(uint i = 0; i < current_ios.size(); i++) {
		if (current_ios[i].get_event_type() == READ_COMMAND) {
			read_commands.push_back(current_ios[i]);
		}
		else if (current_ios[i].get_event_type() == READ_TRANSFER) {
			read_transfers.push_back(current_ios[i]);
		}
		else if (current_ios[i].get_event_type() == WRITE) {
			writes.push_back(current_ios[i]);
		}
		else if (current_ios[i].get_event_type() == ERASE) {
			erases.push_back(current_ios[i]);
		}
	}
	execute_next_batch(erases);
	execute_next_batch(read_transfers);
	execute_next_batch(read_commands);
	handle_writes(writes);
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
void IOScheduler::handle_writes(std::vector<Event>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	while (events.size() > 0) {
		Event event = events.back();
		events.pop_back();
		if (!event.is_garbage_collection_op()) {
			eliminate_conflict_with_any_incoming_gc(event);
		}
		if (!bm.can_write(event)) {
			event.incr_bus_wait_time(1);
			event.incr_time_taken(1);
			io_schedule.push_back(event);
			continue;
		}
		Address page = bm.choose_write_location(event);
		event.set_address(page);
		event.set_noop(false);
		double time = in_how_long_can_this_event_be_scheduled(event);
		if (time == 0) {
			ftl.set_replace_address(event);				// this method might now become redundant.
			adjust_conflict_elimination_structures(event);
			execute_next(event);
		}
		else if (event.get_bus_wait_time() > 50) {
			ftl.set_replace_address(event);				// this method might now become redundant.
			event.incr_bus_wait_time(time);
			event.incr_time_taken(time);
			adjust_conflict_elimination_structures(event);
			execute_next(event);
		}
		else {
			event.incr_bus_wait_time(time);
			event.incr_time_taken(time);
			io_schedule.push_back(event);
		}
	}
}

// if a write to LBA X arrives, while there is already a pending GC operation
// to migrate LBA X, the GC operation becomes redundant, so we cancel it.
void IOScheduler::eliminate_conflict_with_any_incoming_gc(Event const&event) {
	if (!event.is_garbage_collection_op()) {
		pending_writes.insert(event.get_logical_address());
	}
	if (LBA_to_dependencies.count(event.get_logical_address()) == 1) {
		uint dependency_code = LBA_to_dependencies[event.get_logical_address()];
		std::deque<Event> gc_queue = dependencies[dependency_code];
		printf("Incoming write making a pending GC operation unnecessary. LBA: %d\n", event.get_logical_address());
		bool eliminated = false;
		for (uint i = 0; i < gc_queue.size(); i++) {
			Event gc_event = gc_queue[i];
			if (gc_event.get_logical_address() == event.get_logical_address() && gc_event.get_event_type() == WRITE) {
				gc_queue.erase (gc_queue.begin()+i);
				i--;
				eliminated = true;
			}
		}
		// if the write was not found in the gc pending queue, then it must be in io_schedule
		if (!eliminated) {
			for (uint i = 0; i < io_schedule.size(); i++) {
				Event some_event = io_schedule[i];
				if (some_event.is_garbage_collection_op() && some_event.get_logical_address() == event.get_logical_address() && some_event.get_event_type() == WRITE) {
					io_schedule.erase(io_schedule.begin() + i);
					i--;
				}
			}
		}
		LBA_to_dependencies.erase(event.get_logical_address());
	}
}

// invoked upon completion of a write
void IOScheduler::adjust_conflict_elimination_structures(Event const&event) {
	if (event.is_garbage_collection_op()) {
		LBA_to_dependencies.erase(event.get_logical_address());
	} else {
		pending_writes.erase(event.get_logical_address());
	}
}

// executes read_commands, read_transfers and erases
void IOScheduler::execute_next_batch(std::vector<Event>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	for(uint i = 0; i < events.size(); i++) {
		Event event = events[i];
		assert(event.get_event_type() != WRITE);
		if (event.get_event_type() == READ_COMMAND || event.get_event_type() == READ_TRANSFER) {
			ftl.set_read_address(event);
		}
		double time = in_how_long_can_this_event_be_scheduled(event);
		bool can_schedule = can_schedule_on_die(event);
		if (can_schedule && time <= 0) {
			execute_next(event);
		}
		else {
			double bus_wait_time;
			if (time > 0) {
				bus_wait_time = time;
			} else {
				bus_wait_time = BUS_CTRL_DELAY + BUS_DATA_DELAY;
			}
			event.incr_bus_wait_time(bus_wait_time);
			event.incr_time_taken(bus_wait_time);
			io_schedule.push_back(event);
		}
	}
}


enum status IOScheduler::execute_next(Event& event) {
	enum status result = ssd.controller.issue(event);
	if (result == SUCCESS) {
		int application_io_id = event.get_application_io_id();
		if (dependencies[application_io_id].size() > 0) {
			Event dependent = dependencies[application_io_id].front();
			dependent.set_start_time(event.get_start_time() + event.get_time_taken());
			dependencies[application_io_id].pop_front();
			io_schedule.push_back(dependent);
			if (event.get_event_type() == READ_COMMAND && dependent.get_event_type() == READ_TRANSFER) {
				dependent.set_address(event.get_address());
			}
		}
		printf("S ");
	} else {
		printf("F ");
		dependencies.erase(event.get_application_io_id());
	}
	event.print();
	handle_finished_event(event, result);
	return result;
}

// gives time until both the channel and die are clear
double IOScheduler::in_how_long_can_this_event_be_scheduled(Event const& event) const {
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	double channel_finish_time = ssd.bus.get_channel(package_id).get_currently_executing_operation_finish_time();
	double die_finish_time = ssd.getPackages()[package_id].getDies()[die_id].get_currently_executing_io_finish_time();
	double max = std::max(channel_finish_time, die_finish_time);
	double time = max - event.get_start_time() - event.get_time_taken();
	return time <= 0 ? 0 : time;
}

bool IOScheduler::can_schedule_on_die(Event const& event) const {
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	bool busy = ssd.getPackages()[package_id].getDies()[die_id].register_is_busy();
	if (!busy) {
		return true;
	}
	uint application_io = ssd.getPackages()[package_id].getDies()[die_id].get_last_read_application_io();
	return event.get_event_type() == READ_TRANSFER && application_io == event.get_application_io_id();
}

void IOScheduler::handle_finished_event(Event const&event, enum status outcome) {
	if (outcome == FAILURE) {
		return;
	}
	if (event.get_event_type() == WRITE) {
		bm.register_write_outcome(event, outcome);
		ftl.register_write_completion(event, outcome);
	} else if (event.get_event_type() == ERASE) {
		bm.register_erase_outcome(event, outcome);
	} else if (event.get_event_type() == READ_COMMAND) {
		bm.register_read_outcome(event, outcome);
	} else if (event.get_event_type() == READ_TRANSFER) {
		ftl.register_read_completion(event, outcome);
	}
}

// Looks for an idle LUN and schedules writes in it. Works in O(events + LUNs)
/*void IOScheduler::handle_writes(std::vector<Event>& events) {
	if (events.size() == 0) {
		return;
	}
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	double start_time = events.back().get_start_time() + events.back().get_bus_wait_time();
	for (uint i = 0; i < SSD_SIZE; i++) {
		double channel_finish_time = ssd.bus.get_channel(i).get_currently_executing_operation_finish_time();
		if (start_time < channel_finish_time) {
			continue;
		}
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double die_finish_time = ssd.getPackages()[i].getDies()[j].get_currently_executing_io_finish_time();
			if (start_time >= die_finish_time && events.size() > 0) {
				Address free_page = Block_manager_parallel::instance()->get_free_page(i, j);
				Event write = events.back();
				events.pop_back();
				write.set_address(free_page);
				execute_next(write);
			}
		}
	}
	for (uint i = 0; i < events.size(); i++) {
		events[i].incr_bus_wait_time(1);
		events[i].incr_time_taken(1);
		io_schedule.push_back(events[i]);
	}
}*/
