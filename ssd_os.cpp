/*++++----------------------------------------------
 * ssd_os.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;

OperatingSystem::OperatingSystem(vector<Thread*> new_threads)
	: ssd(new Ssd()),
	  events(threads.size()),
	  LBA_to_thread_id_map(),
	  currently_executing_ios_counter(0),
	  currently_pending_ios_counter(0),
	  last_dispatched_event_minimal_finish_time(1),
	  threads(new_threads),
	  thread_dependencies(new_threads.size(), queue<Thread*>())
{
	assert(threads.size() > 0);
	for (uint i = 0; i < threads.size(); i++) {
		events[i] = threads[i]->run();
		if (events[i]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	ssd->set_operating_system(this);
}

OperatingSystem::OperatingSystem(vector<queue<Thread*> > threads_dependencies) :
	ssd(new Ssd()),
	events(threads.size()),
	LBA_to_thread_id_map(),
	currently_executing_ios_counter(0),
	currently_pending_ios_counter(0),
	last_dispatched_event_minimal_finish_time(1),
	threads(threads_dependencies.size()),
	thread_dependencies(threads_dependencies)
{
	assert(thread_dependencies.size() > 0);
	for (uint i = 0; i < thread_dependencies.size(); i++) {
		threads[i] = thread_dependencies[i].front();
		thread_dependencies[i].pop();
		events[i] = threads[i]->run();
		if (events[i]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	ssd->set_operating_system(this);
}

OperatingSystem::~OperatingSystem() {
	for (uint i = 0; i < threads.size(); i++) {
		delete threads[i];
		delete events[i];
	}
	delete ssd;
}

void OperatingSystem::run() {
	do {
		int thread_id = pick_event_with_shortest_start_time();
		if (thread_id == -1 || (currently_executing_ios_counter > 0 && last_dispatched_event_minimal_finish_time < events[thread_id]->get_start_time())) {
			ssd->progress_since_os_is_idle();
		}
		else {
			dispatch_event(thread_id);
		}
	} while (currently_executing_ios_counter > 0 || currently_pending_ios_counter > 0);
}

int OperatingSystem::pick_event_with_shortest_start_time() {
	double soonest_time = 1000000000;
	int thread_id = -1;

	for (uint i = 0; i < events.size(); i++) {
		Event* e = events[i];
		if (e != NULL && e->get_event_type() != NOT_VALID && e->get_start_time() < soonest_time && LBA_to_thread_id_map.count(e->get_logical_address()) == 0) {
			soonest_time = events[i]->get_start_time();
			thread_id = i;
		}
	}
	return thread_id;
}

void OperatingSystem::dispatch_event(int thread_id) {
	Event* event = events[thread_id];
	currently_executing_ios_counter++;
	currently_pending_ios_counter--;
	last_dispatched_event_minimal_finish_time = get_event_minimal_completion_time(event);
	LBA_to_thread_id_map[event->get_logical_address()] = thread_id;
	ssd->event_arrive(event);
	events[thread_id] = threads[thread_id]->run();
	if (events[thread_id] != NULL && event->get_event_type() != NOT_VALID) {
		currently_pending_ios_counter++;
	}
}

void OperatingSystem::register_event_completion(Event* event) {
	assert(LBA_to_thread_id_map.count(event->get_logical_address()) == 1);
	uint thread_id = LBA_to_thread_id_map[event->get_logical_address()];
	LBA_to_thread_id_map.erase(event->get_logical_address());
	threads[thread_id]->register_event_completion(event);

	if (threads[thread_id]->is_finished() && thread_dependencies[thread_id].size() > 0) {
		delete threads[thread_id];
		threads[thread_id] = thread_dependencies[thread_id].front();
		thread_dependencies[thread_id].pop();
		threads[thread_id]->set_time(event->get_current_time());
	}

	if (events[thread_id] == NULL) {
		events[thread_id] = threads[thread_id]->run();
		if (events[thread_id] != NULL && events[thread_id]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	currently_executing_ios_counter--;
	delete event;
}

double OperatingSystem::get_event_minimal_completion_time(Event const*const event) const {
	double result = event->get_start_time();
	if (event->get_event_type() == WRITE) {
		result += 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY + PAGE_WRITE_DELAY;
	}
	else if (event->get_event_type() == READ) {
		result += 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY + PAGE_READ_DELAY;
	}
	return result;
}

