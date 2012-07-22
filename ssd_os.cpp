/*
 * ssd_os.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;

OperatingSystem::OperatingSystem(vector<Thread*> new_threads)
	: ssd(ssd), threads(new_threads),
	  events(threads.size()),
	  LBA_to_thread_id_map(),
	  currently_executing_ios_counter(0),
	  currently_pending_ios_counter(0)
{
	for (uint i = 0; i < threads.size(); i++) {
		events[i] = threads[i]->issue_next_io();
		if (events[i]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
}

OperatingSystem::~OperatingSystem() {

	for (uint i = 0; i < threads.size(); i++) {
		delete threads[i];
		delete events[i];
	}
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

		if (thread_id == -1) {
			ssd->progress_since_os_is_idle();
		} else {
			currently_executing_ios_counter++;
			currently_pending_ios_counter--;
			Event* event = events[thread_id];
			LBA_to_thread_id_map[event->get_logical_address()] = thread_id;
			events[thread_id] = NULL;
			ssd->event_arrive(event);
		}
	} while (currently_executing_ios_counter > 0 || currently_pending_ios_counter > 0);
}

void OperatingSystem::register_event_completion(Event* event) {
	assert(LBA_to_thread_id_map.count(event->get_logical_address()) == 1);
	uint thread_id = LBA_to_thread_id_map[event->get_logical_address()];
	LBA_to_thread_id_map.erase(event->get_logical_address());
	threads[thread_id]->register_event_completion(event);
	events[thread_id] = threads[thread_id]->issue_next_io();
	currently_executing_ios_counter--;
	if (events[thread_id] != NULL && events[thread_id]->get_event_type() != NOT_VALID) {
		currently_pending_ios_counter++;
	}
}

void OperatingSystem::set_ssd(Ssd * new_ssd) {
	ssd = new_ssd;
}

