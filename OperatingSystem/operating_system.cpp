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
	  threads(new_threads),
	  NUM_WRITES_TO_STOP_AFTER(UNDEFINED),
	  num_writes_completed(0),
	  time_of_last_event_completed(1),
	  counter_for_user(0)
{
	assert(threads.size() > 0);
	for (uint i = 0; i < threads.size(); i++) {
		events[i] = threads[i]->run();
		if (events[i] != NULL && events[i]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}
	ssd->set_operating_system(this);
	assert(MAX_SSD_QUEUE_SIZE >= SSD_SIZE * PACKAGE_SIZE);
}

OperatingSystem::~OperatingSystem() {
	for (uint i = 0; i < threads.size(); i++) {
		Thread* t = threads[i];
		if (t != NULL) {
			delete t;
		}
	}
	threads.clear();
	for (uint i = 0; i < events.size(); i++) {
		Event* e = events[i];
		if (e != NULL) {
			delete e;
		}
	}
	events.clear();
	delete ssd;
}

void OperatingSystem::run() {
	const int idle_limit = 100000000; // 10 minutes
	int idle_time = 0;
	bool finished_experiment, still_more_work;
	do {

		int thread_id = pick_unlocked_event_with_shortest_start_time();

		bool no_pending_event = thread_id == UNDEFINED;
		//bool pending_event_starts_much_later = thread_id != UNDEFINED && currently_executing_ios_counter > 0 && last_dispatched_event_minimal_finish_time < events[thread_id]->get_start_time();
		bool queue_is_full = currently_executing_ios_counter >= MAX_SSD_QUEUE_SIZE;

		if (no_pending_event /* || pending_event_starts_much_later */|| queue_is_full) {
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
			idle_time = 0;
			dispatch_event(thread_id);
			/*if (currently_executing_ios_counter < MAX_SSD_QUEUE_SIZE) {
				printf("Limit on number of queued application IOs exceeded:  %d\n", currently_executing_ios_counter);
				throw "Limit on number of queued application IOs exceeded:";
			}*/
		}

		if (counter_for_user * 10000 < num_writes_completed) {
			printf("finished app writes:  %d\n", counter_for_user * 10000);
			counter_for_user++;
		}

		finished_experiment = NUM_WRITES_TO_STOP_AFTER != UNDEFINED && NUM_WRITES_TO_STOP_AFTER <= num_writes_completed;
		still_more_work = currently_executing_ios_counter > 0 || currently_pending_ios_counter > 0;
		//printf("num_writes   %d\n", num_writes_completed);
	} while (!finished_experiment && still_more_work);
	//VisualTracer::get_instance()->print_horizontally_with_breaks();
}

int OperatingSystem::pick_unlocked_event_with_shortest_start_time() {
	double soonest_time = numeric_limits<double>::max();
	int thread_id = UNDEFINED;
	int num_pending_events_confirmation = 0;
	for (uint i = 0; i < events.size(); i++) {
		Event* e = events[i];
		if (e != NULL && e->get_start_time() < soonest_time && !is_LBA_locked(e->get_logical_address()) ) {
			soonest_time = events[i]->get_start_time();
			thread_id = i;
		}
		if (e != NULL && e->get_event_type() != NOT_VALID) {
			num_pending_events_confirmation++;
		}
	}
	if (num_pending_events_confirmation != currently_pending_ios_counter) {
		int i = 0;
		i++;
	}
	//assert(num_pending_events_confirmation == currently_pending_ios_counter);
	return thread_id;
}

void OperatingSystem::dispatch_event(int thread_id) {
	Event* event = events[thread_id];

	currently_executing_ios_counter++;
	currently_executing_ios.insert(event->get_application_io_id());
	currently_pending_ios_counter--;
	assert(currently_pending_ios_counter >= 0);

	//Thread* dispatching_thread = threads[thread_id];
	//dispatching_thread->set_time(event->get_ssd_submission_time());

	double min_completion_time = get_event_minimal_completion_time(event);
	last_dispatched_event_minimal_finish_time = max(last_dispatched_event_minimal_finish_time, min_completion_time);

	map<long, queue<uint> >& map = get_relevant_LBA_to_thread_map(event->get_event_type());
	map[event->get_logical_address()].push(thread_id);

	//printf("dispatching:\t"); event->print();

	ssd->event_arrive(event);
	events[thread_id] = threads[thread_id]->run();
	if (events[thread_id] != NULL && event->get_event_type() != NOT_VALID) {
		currently_pending_ios_counter++;
	}
}

void OperatingSystem::register_event_completion(Event* event) {
	bool queue_was_full = currently_executing_ios_counter == MAX_SSD_QUEUE_SIZE;
	currently_executing_ios_counter--;
	assert(currently_executing_ios_counter >= 0);   // there is currently a bug where this number goes below 0. need to fix it.
	currently_executing_ios.erase(event->get_application_io_id());

	//printf("finished:\t"); event->print();

	ulong la = event->get_logical_address();
	map<long, queue<uint> >& map = get_relevant_LBA_to_thread_map(event->get_event_type());
	uint thread_id = map[la].front();
	map[la].pop();
	if (map[la].size() == 0) {
		map.erase(la);
	}

	Thread* thread = threads[thread_id];
	thread->register_event_completion(event);

	if (event->get_event_type() == WRITE) {
		num_writes_completed++;
	}

	if (thread->is_finished() && thread->get_follow_up_threads().size() > 0) {
		/*if (PRINT_LEVEL >= 1)*/ printf("Switching to new follow up thread\n");
		vector<Thread*> follow_up_threads = thread->get_follow_up_threads();
		if (follow_up_threads.size() > 0) {
			threads[thread_id] = follow_up_threads[0];
			threads[thread_id]->set_time(event->get_current_time());
		}
		for (uint i = 1; i < follow_up_threads.size(); i++) {
			follow_up_threads[i]->set_time(event->get_current_time());
			threads.push_back(follow_up_threads[i]);
			Event* first_thread_event = follow_up_threads[i]->run();
			events.push_back(first_thread_event);
			if (first_thread_event != NULL) {
				currently_pending_ios_counter++;
			}
		}
		thread->get_follow_up_threads().clear();
		delete thread;
	}

	if (queue_was_full) {
		for (uint i = 0; i < threads.size(); i++) {
			Thread* t = threads[i];
			if (!t->is_finished()) {
				double thread_time = t->get_time();
				if (thread_time < event->get_current_time() + 1) {
					t->set_time(event->get_current_time() + 1);
				}
				Event* e = events[i];
				if (e != NULL) {
					double diff = event->get_current_time() - e->get_start_time() ;
					e->incr_os_wait_time(diff);
					e->incr_time_taken(diff);
				}
			}
		}
	}

	if (events[thread_id] == NULL) {
		events[thread_id] = threads[thread_id]->run();
		if (events[thread_id] != NULL && events[thread_id]->get_event_type() != NOT_VALID) {
			currently_pending_ios_counter++;
		}
	}

	time_of_last_event_completed = max(time_of_last_event_completed, event->get_current_time());

	int thread_with_soonest_event = pick_unlocked_event_with_shortest_start_time();
	if (thread_with_soonest_event != UNDEFINED) {
		dispatch_event(thread_with_soonest_event);
	}

	delete event;
}

void OperatingSystem::set_num_writes_to_stop_after(long num_writes) {
	NUM_WRITES_TO_STOP_AFTER = num_writes;
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

double OperatingSystem::get_total_runtime() const {
	return time_of_last_event_completed;
}

