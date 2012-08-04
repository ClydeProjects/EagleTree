/*++++----------------------------------------------
 * ssd_os.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;

OperatingSystem::OperatingSystem(vector<Thread*> new_threads)
	: ssd(new Ssd()), threads(new_threads),
	  events(threads.size()),
	  LBA_to_thread_id_map(),
	  currently_executing_ios_counter(0),
	  currently_pending_ios_counter(0)
{
	assert(threads.size() > 0);
	for (uint i = 0; i < threads.size(); i++) {
		events[i] = threads[i]->issue_next_io();
		if (events[i]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	last_dispatched_event_minimal_finish_time = events[0]->get_start_time();
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
		double soonest_time = 1000000000;
		int thread_id = -1;

		for (uint i = 0; i < events.size(); i++) {
			Event* e = events[i];
			if (e != NULL && e->get_event_type() != NOT_VALID && e->get_start_time() < soonest_time && LBA_to_thread_id_map.count(e->get_logical_address()) == 0) {
				soonest_time = events[i]->get_start_time();
				thread_id = i;
			}
		}
		Event* event = events[thread_id];

		if (thread_id == -1 || (currently_executing_ios_counter > 0 && last_dispatched_event_minimal_finish_time < event->get_start_time())) {
			ssd->progress_since_os_is_idle();
		}
		else {
			currently_executing_ios_counter++;
			currently_pending_ios_counter--;
			last_dispatched_event_minimal_finish_time = get_event_minimal_completion_time(event);
			LBA_to_thread_id_map[event->get_logical_address()] = thread_id;
			ssd->event_arrive(event);
			events[thread_id] = threads[thread_id]->issue_next_io();
			if (events[thread_id] != NULL && event->get_event_type() != NOT_VALID) {
				currently_pending_ios_counter++;
			}
		}

	} while (currently_executing_ios_counter > 0 || currently_pending_ios_counter > 0);
}

void OperatingSystem::register_event_completion(Event* event) {
	assert(LBA_to_thread_id_map.count(event->get_logical_address()) == 1);
	uint thread_id = LBA_to_thread_id_map[event->get_logical_address()];
	LBA_to_thread_id_map.erase(event->get_logical_address());
	threads[thread_id]->register_event_completion(event);
	if (events[thread_id] == NULL) {
		events[thread_id] = threads[thread_id]->issue_next_io();
		if (events[thread_id] != NULL && events[thread_id]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	currently_executing_ios_counter--;
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

