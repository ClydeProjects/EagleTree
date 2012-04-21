/*
 * ssd_io_scheduler.cpp
 *
 *  Created on: Apr 15, 2012
 *      Author: niv
 */

#include "ssd.h"

using namespace ssd;

IOScheduler::IOScheduler(Controller &controller) :
	controller(controller)
{}

IOScheduler::~IOScheduler(){}

IOScheduler *IOScheduler::inst = NULL;

void IOScheduler::instance_initialize(Controller &controller)
{
	IOScheduler::inst = new IOScheduler(controller);
}

IOScheduler *IOScheduler::instance()
{
	return IOScheduler::inst;
}

void IOScheduler::schedule_independent_event(Event& event) {
	io_schedule.push(event);

	//printf("top id: %d   start time: %f\n", io_schedule.top().get_id(), io_schedule.top().get_start_time());
	//printf("new id: %d   start time: %f\n", event.get_id(), event.get_start_time());

	while (io_schedule.top().get_start_time() + 2 < event.get_start_time()) {
		execute_current_waiting_ios();
	}
}



void IOScheduler::schedule_dependency(Event& event) {

	int application_io_id = event.get_application_io_id();
	assert(application_io_id > 0);

	if (event.get_event_type() == READ) {
		event.set_event_type(READ_TRANSFER);
		Event* read_command = new Event(READ_COMMAND, event.get_logical_address(), event.get_size(), event.get_start_time());
		read_command->set_address(event.get_address());
		read_command->set_application_io_id(application_io_id);
		dependencies[application_io_id].push(*read_command);
	}
	dependencies[application_io_id].push(event);
	//fprintf(stdout, "count: %d \n", (int)dependencies[application_io_id].size());
}

void IOScheduler::launch_dependency(uint application_io_id) {
	Event first = dependencies[application_io_id].front();
	dependencies[application_io_id].pop();
	schedule_independent_event(first);
}

void IOScheduler::finish() {
	while (io_schedule.size() > 0) {
		execute_current_waiting_ios();
	}
}

std::vector<Event> IOScheduler::gather_current_waiting_ios() {
	Event top = io_schedule.top();
	io_schedule.pop();
	double start_time = top.get_start_time();
	std::vector<Event> current_ios;
	current_ios.push_back(top);
	while (io_schedule.size() > 0 && start_time + 1 > io_schedule.top().get_start_time()) {
		Event current_top = io_schedule.top();
		io_schedule.pop();
		current_ios.push_back(current_top);
	}
	return current_ios;
}

void IOScheduler::execute_current_waiting_ios() {
	std::vector<Event> current_ios = gather_current_waiting_ios();
	for(uint i = 0; i < current_ios.size(); i++) {
		Event event = current_ios[i];
		double time = controller.in_how_long_can_this_event_be_scheduled(event);
		if (time <= 0) {
			execute_next(event);
		} else {
			event.incr_bus_wait_time(time);
			event.incr_time_taken(time);
			io_schedule.push(event);
		}
	}
}

void IOScheduler::execute_next(Event& event) {
	enum status result = controller.issue(event);
	if (result == SUCCESS) {
		int application_io_id = event.get_application_io_id();
		if (dependencies[application_io_id].size() > 0) {
			Event dependent = dependencies[application_io_id].front();
			dependent.set_start_time(event.get_start_time() + event.get_time_taken());
			dependencies[application_io_id].pop();
			io_schedule.push(dependent);
		}
	} else {
		dependencies.erase(event.get_application_io_id());
	}
	event.print();
}



