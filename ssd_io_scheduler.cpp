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
	bm(ssd, ftl)
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

bool myfunction (const Event* i, const Event* j) {
	return i->get_current_time() > j->get_current_time();
}

bool bus_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
}

void IOScheduler::schedule_dependent_events(std::queue<Event*>& events) {
	uint dependency_code = events.back()->get_application_io_id();
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
			dependencies[dependency_code].push_back(read_command);
		}
		if (event->is_garbage_collection_op() && event->get_event_type() == WRITE) {
			LBA_to_dependencies[event->get_logical_address()] = dependency_code;
		}
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
	while (io_schedule.size() > 0 && io_schedule.back()->get_current_time() + 1 <= first->get_start_time()) {
		execute_current_waiting_ios();
	}
	assert(events.empty());
}

void IOScheduler::schedule_independent_event(Event* event) {
	std::queue<Event*> eventVec;
	eventVec.push(event);
	schedule_dependent_events(eventVec);
}

void IOScheduler::finish(double start_time) {
	while (io_schedule.size() > 0 && io_schedule.back()->get_current_time() + 1 < start_time) {
		execute_current_waiting_ios();
	}
}

void IOScheduler::progess() {
	if (io_schedule.size() > 0) {
		execute_current_waiting_ios();
	}
}

std::vector<Event*> IOScheduler::gather_current_waiting_ios() {
	std::sort(io_schedule.begin(), io_schedule.end(), myfunction);
	double start_time = io_schedule.back()->get_current_time();
	std::vector<Event*> current_ios;
	while (io_schedule.size() > 0 && start_time + 1 > io_schedule.back()->get_current_time()) {
		Event* current_top = io_schedule.back();
		io_schedule.pop_back();
		current_ios.push_back(current_top);
	}
	return current_ios;
}



void IOScheduler::execute_current_waiting_ios() {
	assert(io_schedule.size() > 0);
	vector<Event*> current_ios = gather_current_waiting_ios();

	vector<Event*> read_commands;
	vector<Event*> read_transfers;
	vector<Event*> writes;
	vector<Event*> erases;
	for(uint i = 0; i < current_ios.size(); i++) {
		if (current_ios[i]->get_event_type() == READ_COMMAND) {
			read_commands.push_back(current_ios[i]);
		}
		else if (current_ios[i]->get_event_type() == READ_TRANSFER) {
			read_transfers.push_back(current_ios[i]);
		}
		else if (current_ios[i]->get_event_type() == WRITE) {
			writes.push_back(current_ios[i]);
		}
		else if (current_ios[i]->get_event_type() == ERASE) {
			erases.push_back(current_ios[i]);
		}
	}
	//printf("\n -------------------------------- \n");
	execute_next_batch(erases);
	execute_next_batch(read_commands);
	execute_next_batch(read_transfers);
	handle_writes(writes);
}

// Looks for an idle LUN and schedules writes in it. Works in O(events * LUNs), but also handles overdue events. Using this for now for simplicity.
void IOScheduler::handle_writes(std::vector<Event*>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	while (events.size() > 0) {
		Event* event = events.back();
		events.pop_back();
		if (event->get_bus_wait_time() == 0) {
			bm.register_write_arrival(*event);
		}
		assert(event->get_event_type() == WRITE);
		ftl.set_replace_address(*event);
		//bm.register_write_arrival(event);
		if (!event->is_garbage_collection_op()) {
			eliminate_conflict_with_any_incoming_gc(event);
		}
		pair<double, Address> result = bm.write(*event);
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
}

// if a write to LBA X arrives, while there is already a pending GC operation
// to migrate LBA X, the GC operation becomes redundant, so we cancel it.
void IOScheduler::eliminate_conflict_with_any_incoming_gc(Event * event) {
	assert(!event->is_garbage_collection_op());
	if (LBA_to_dependencies.count(event->get_logical_address()) == 0) {
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

	printf("Write makes pending GC unnecessary. LBA: %d   removed %d events", event->get_logical_address(), num_events_eliminated);
	event->get_replace_address().print();
	printf("\n");

	event->set_garbage_collection_op(true);
	assert(num_events_eliminated > 0);

	LBA_to_dependencies.erase(event->get_logical_address());
}

// invoked upon completion of a write
void IOScheduler::adjust_conflict_elimination_structures(Event const*const event) {
	if (event->is_garbage_collection_op()) {
		LBA_to_dependencies.erase(event->get_logical_address());
	}
}

// executes read_commands, read_transfers and erases
void IOScheduler::execute_next_batch(std::vector<Event*>& events) {
	std::sort(events.begin(), events.end(), bus_wait_time_comparator);
	for(uint i = 0; i < events.size(); i++) {
		Event* event = events[i];
		assert(event->get_event_type() != WRITE);
		if (event->get_event_type() == READ_COMMAND || event->get_event_type() == READ_TRANSFER) {
			ftl.set_read_address(*event);
		}
		double time = bm.in_how_long_can_this_event_be_scheduled(event->get_address(), event->get_current_time());
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
		int application_io_id = event->get_application_io_id();
		if (dependencies[application_io_id].size() > 0) {
			Event* dependent = dependencies[application_io_id].front();
			dependent->set_start_time(event->get_current_time());
			dependencies[application_io_id].pop_front();
			io_schedule.push_back(dependent);
			if (event->get_event_type() == READ_COMMAND && dependent->get_event_type() == READ_TRANSFER) {
				dependent->set_address(event->get_address());
			}
		}
		printf("S ");
	} else {
		printf("F ");
		dependencies.erase(event->get_application_io_id());
	}

	/*double io_start_time = event->get_start_time() + event->get_bus_wait_time();
	assert(io_start_time >=  time_of_last_IO_execution_start);
	time_of_last_IO_execution_start = io_start_time;*/

	event->print();
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
		return;
	}
	VisualTracer::get_instance()->register_completed_event(*event);
	if (event->get_event_type() == WRITE) {
		ftl.register_write_completion(*event, outcome);
		bm.register_write_outcome(*event, outcome);
	} else if (event->get_event_type() == ERASE) {
		bm.register_erase_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_COMMAND) {
		bm.register_read_outcome(*event, outcome);
	} else if (event->get_event_type() == READ_TRANSFER) {
		ftl.register_read_completion(*event, outcome);
	}
	StatisticsGatherer::get_instance()->register_completed_event(*event);

	if (event->is_original_application_io()) {
		ssd.register_event_completion(event);
	}
	else {
		delete event;
	}
}
