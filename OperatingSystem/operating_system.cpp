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
	  events(0),
	  currently_executing_ios_counter(0),
	  currently_pending_ios_counter(0),
	  num_locked_events(0),
	  last_dispatched_event_minimal_finish_time(1),
	  threads(new_threads),
	  num_writes_to_stop_after(UNDEFINED),
	  num_writes_completed(0)
{
	assert(threads.size() > 0);
	for (uint thread_id = 0; thread_id < threads.size(); thread_id++) {
		get_next_event(thread_id);
	}
	ssd->set_operating_system(this);
}

OperatingSystem::~OperatingSystem() {
	for (uint i = 0; i < threads.size(); i++) {
		//threads[i]->print_thread_stats();
		delete threads[i];
		//delete events[i];
	}
	delete ssd;
}

// create a method that gets thread_id. Runs thread,
// if locked, puts in queue, and calls run again until there is an unlocked event.
// this is problematic, if all lbas are locked.
// May create a locked event structure, consisting of an event pointer and thread id.
// have a map from locked lba to queue of pending events awaiting the lock.
// invariant: if there are x events executing, there cannot be more than x locked lbas.
// when lock is released, submit event.

void OperatingSystem::run() {
	const int idle_limit = 1000000; // 10 minutes
	int idle_time = 0;
	bool finished_experiment, still_more_work;
	do {
		os_event event = pick_unlocked_event_with_shortest_start_time();

		if (event.pending_event == NULL) {

			if (idle_time >= idle_limit) {
				printf("Idle time limit reached\n");
				printf("Running IOs:\n");
				for (set<uint>::iterator it = currently_executing_ios.begin(); it != currently_executing_ios.end(); it++) {
					printf("%d ", *it);
				}
				throw;
			}

			ssd->progress_since_os_is_waiting();
			idle_time++;
		}
		else {
			dispatch_event(event);
			idle_time = 0;
		}
		finished_experiment = num_writes_to_stop_after != UNDEFINED && num_writes_to_stop_after <= num_writes_completed;
		still_more_work = currently_executing_ios_counter > 0 || currently_pending_ios_counter > 0;
		//printf("num_writes   %d\n", num_writes_completed);
	} while (/*!finished_experiment &&*/ still_more_work);
	printf(" ");
}

void OperatingSystem::get_next_event(int thread_id) {
	Event* event = threads[thread_id]->run();
	while (event != NULL && is_LBA_locked(event->get_logical_address())) {
		os_event le = os_event(thread_id, event);
		assert(event->get_event_type() != NOT_VALID);
		//currently_pending_ios_counter++;
		printf("locking:\t"); event->print();
		locked_events[event->get_logical_address()].push(le);
		event = threads[thread_id]->run();
		num_locked_events++;
		printf("num_locked_events:\t%d\n", num_locked_events);
		if (num_locked_events >= MAX_OS_NUM_LOCKS) {
			throw "The number of locks held by the system exceeded the permissible number";
		}
	}
	if (event != NULL) {
		os_event le = os_event(thread_id, event);
		events.push_back(le);
		lba_locks[event->get_logical_address()] = thread_id;
		currently_pending_ios_counter++;
	}
}



os_event OperatingSystem::pick_unlocked_event_with_shortest_start_time() {
	double soonest_time = numeric_limits<double>::max();
	int chosen = UNDEFINED;
	int num_pending_events_confirmation = 0;
	for (uint i = 0; i < events.size(); i++) {
		Event* e = events[i].pending_event;
		bool can_schedule = currently_executing_ios_counter == 0 || last_dispatched_event_minimal_finish_time >= e->get_start_time();
		//assert(!is_LBA_locked(e->get_logical_address()));
		if (can_schedule && e->get_current_time() < soonest_time) {
			soonest_time = e->get_current_time();
			chosen = i;
		}
		if (e != NULL && e->get_event_type() != NOT_VALID) {
			num_pending_events_confirmation++;
		}
	}
	if (num_pending_events_confirmation != currently_pending_ios_counter) {
		assert(false);
	}


	os_event chosen_event;
	if (chosen != UNDEFINED) {
		chosen_event = events[chosen];
		events.erase(events.begin() + chosen);
	}

	return chosen_event;
}

void OperatingSystem::dispatch_event(os_event event) {
	Event* ssd_event = event.pending_event;
	//printf("dispatching event: "); ssd_event->print();
	currently_executing_ios_counter++;
	currently_executing_ios.insert(ssd_event->get_id());
	currently_pending_ios_counter--;
	double min_completion_time = get_event_minimal_completion_time(ssd_event);
	last_dispatched_event_minimal_finish_time = max(last_dispatched_event_minimal_finish_time, min_completion_time);
	assert(ssd_event->get_event_type() != NOT_VALID);
	ssd->event_arrive(ssd_event);
	get_next_event(event.thread_id);
}

int OperatingSystem::release_lock(Event* event_just_finished) {
	long lba = event_just_finished->get_logical_address();
	int thread_id = lba_locks[lba];

	if (locked_events.count(lba) == 1) {
		os_event released_event = locked_events[lba].front();
		locked_events[lba].pop();
		lba_locks[lba] = released_event.thread_id;
		double os_wait_time = event_just_finished->get_current_time() - released_event.pending_event->get_current_time();
		released_event.pending_event->incr_os_wait_time(os_wait_time);
		released_event.pending_event->incr_time_taken(os_wait_time);
		events.push_back(released_event);
		printf("releasing lock on:  "); released_event.pending_event->print();
		if (locked_events[lba].size() == 0) {
			locked_events.erase(lba);
		}
		num_locked_events--;
		currently_pending_ios_counter++;
		printf("num_locked_events:\t%d\n", num_locked_events);
	} else {
		lba_locks.erase(lba);
	}
	return thread_id;
}

void OperatingSystem::register_event_completion(Event* event) {
	int thread_id = release_lock(event);
	Thread* thread = threads[thread_id];
	thread->register_event_completion(event);

	if (event->get_event_type() == WRITE) {
		num_writes_completed++;
	}

	if (!thread->is_finished()) {
		get_next_event(thread_id);
	}
	else if (thread->is_finished() && thread->get_follow_up_threads().size() > 0) {
		if (PRINT_LEVEL >= 1) printf("Switching to new follow up thread\n");
		vector<Thread*> follow_up_threads = thread->get_follow_up_threads();
		threads[thread_id] = follow_up_threads[0];
		threads[thread_id]->set_time(event->get_current_time());

		for (uint i = 1; i < follow_up_threads.size(); i++) {
			follow_up_threads[i]->set_time(event->get_current_time());
			threads.push_back(follow_up_threads[i]);
			get_next_event(threads.size() - 1);
		}
		delete thread;
	}



	currently_executing_ios_counter--;
	currently_executing_ios.erase(event->get_id());

	time_of_last_event_completed = max(time_of_last_event_completed, event->get_current_time());

	delete event;
}

void OperatingSystem::set_num_writes_to_stop_after(long num_writes) {
	num_writes_to_stop_after = num_writes;
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

bool OperatingSystem::is_LBA_locked(ulong lba) {
	if (!OS_LOCK) {
		return false;
	} else {
		return lba_locks.count(lba) == 1;
	}
}

double OperatingSystem::get_total_runtime() const {
	return time_of_last_event_completed;
}

