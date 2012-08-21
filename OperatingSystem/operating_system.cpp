/*++++----------------------------------------------
 * ssd_os.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

OperatingSystem::OperatingSystem(vector<Thread*> new_threads)
	: ssd(new Ssd()),
	  events(threads.size()),
	  currently_executing_ios_counter(0),
	  currently_pending_ios_counter(0),
	  last_dispatched_event_minimal_finish_time(1),
	  threads(new_threads)
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

OperatingSystem::~OperatingSystem() {
	for (uint i = 0; i < threads.size(); i++) {
		threads[i]->print_thread_stats();
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
		if (e != NULL && e->get_event_type() != NOT_VALID && e->get_start_time() < soonest_time && !is_LBA_locked(e->get_logical_address()) ) {
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

	map<long, queue<uint> >& map = get_relevant_LBA_to_thread_map(event->get_event_type());
	map[event->get_logical_address()].push(thread_id);

	ssd->event_arrive(event);
	events[thread_id] = threads[thread_id]->run();
	if (events[thread_id] != NULL && event->get_event_type() != NOT_VALID) {
		currently_pending_ios_counter++;
	}
}

void OperatingSystem::register_event_completion(Event* event) {

	ulong la = event->get_logical_address();
	map<long, queue<uint> >& map = get_relevant_LBA_to_thread_map(event->get_event_type());
	uint thread_id = map[la].front();
	map[la].pop();
	if (map[la].size() == 0) {
		map.erase(la);
	}

	Thread* thread = threads[thread_id];
	thread->register_event_completion(event);

	if (thread->is_finished() && thread->get_follow_up_threads().size() > 0) {
		printf("Switching to new follow up thread\n");
		vector<Thread*> follow_up_threads = thread->get_follow_up_threads();
		threads[thread_id] = follow_up_threads[0];
		threads[thread_id]->set_time(event->get_current_time());
		for (uint i = 1; i < follow_up_threads.size(); i++) {
			follow_up_threads[i]->set_time(event->get_current_time());
			threads.push_back(follow_up_threads[i]);
			events.push_back(follow_up_threads[i]->run());
		}
		delete thread;
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

map<long, queue<uint> >& OperatingSystem::get_relevant_LBA_to_thread_map(event_type type) {
	if (type == READ || type == READ_TRANSFER) {
		return read_LBA_to_thread_id;
	}
	else if (type == WRITE) {
		return write_LBA_to_thread_id;
	}
	else {
		return trim_LBA_to_thread_id;
	}
}

bool OperatingSystem::is_LBA_locked(ulong lba) {
	if (!OS_LOCK) {
		return false;
	} else {
		return read_LBA_to_thread_id.count(lba) > 0 || write_LBA_to_thread_id.count(lba) > 0 || trim_LBA_to_thread_id.count(lba) > 0;
	}
}
