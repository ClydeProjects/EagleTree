/*++++----------------------------------------------
 * ssd_os.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

int OperatingSystem::thread_id_generator = 0;

OperatingSystem::OperatingSystem()
	: ssd(new Ssd()),
	  threads(),
	  NUM_WRITES_TO_STOP_AFTER(UNDEFINED),
	  num_writes_completed(0),
	  counter_for_user(1),
	  idle_time(0),
	  time(0),
	  scheduler(new FAIR_OS_Scheduler())
{
	ssd->set_operating_system(this);
	thread_id_generator = 0;
	//assert(MAX_SSD_QUEUE_SIZE >= SSD_SIZE * PACKAGE_SIZE);
}

void OperatingSystem::set_threads(vector<Thread*> new_threads) {
	for (uint i = 0; i < threads.size(); i++) {
		delete threads[i];
	}
	num_writes_completed = 0;
	threads.clear();
	assert(new_threads.size() > 0);
	for (uint i = 0; i < new_threads.size(); i++) {
		Thread* t = new_threads[i];
		t->init(this, time);
		int new_id = ++thread_id_generator;
		threads[new_id] = t;
	}
}

OperatingSystem::~OperatingSystem() {
	delete ssd;
	map<int, Thread*>::iterator i = threads.begin();
	for (; i != threads.end(); i++) {
		Thread* t = (*i).second;
		delete t;
	}
	threads.clear();
	delete scheduler;
}

void OperatingSystem::run() {
	const int idle_limit = 5000000;
	bool finished_experiment, still_more_work;
	do {
		int thread_id = scheduler->pick(threads);
		bool no_pending_event = thread_id == UNDEFINED;
		bool queue_is_full = currently_executing_ios.size() >= MAX_SSD_QUEUE_SIZE;
		if (no_pending_event || queue_is_full) {
			if (idle_time > 100000 && idle_time % 100000 == 0) {
				printf("Idle for %f seconds. No_pending_event=%d  Queue_is_full=%d\n", (double) idle_time / 1000000, no_pending_event, queue_is_full);
			}
			if (idle_time >= idle_limit) {
				printf("Idle time limit reached\nRunning IOs:");
				for (set<uint>::iterator it = currently_executing_ios.begin(); it != currently_executing_ios.end(); it++) {
					printf("%d ", *it);
				}
				printf("\n");
				throw;
			}
			ssd->progress_since_os_is_waiting();
			idle_time++;
		}
		else {
			dispatch_event(thread_id);
		}

		if ((double)num_writes_completed / NUM_WRITES_TO_STOP_AFTER > (double)counter_for_user / 10.0) {
			printf("finished %d%%.\t\tNum writes completed:  %d \n", counter_for_user * 10, num_writes_completed);
			if (counter_for_user == 9) {
				//PRINT_LEVEL = 1;
				//VisualTracer::get_instance()->print_horizontally(10000);
				//VisualTracer::get_instance()->print_horizontally_with_breaks_last(10000);
			}
			counter_for_user++;
		}

		finished_experiment = NUM_WRITES_TO_STOP_AFTER != UNDEFINED && NUM_WRITES_TO_STOP_AFTER <= num_writes_completed;
		still_more_work = currently_executing_ios.size() > 0 || threads.size() > 0;
		//printf("num_writes   %d\n", num_writes_completed);
	} while (!finished_experiment && still_more_work);

	map<int, Thread*>::iterator i = threads.begin();
	for (; i != threads.end(); i++) {
		Thread* t = (*i).second;
		t->set_finished();
	}
}


void OperatingSystem::dispatch_event(int thread_id) {
	idle_time = 0;
	Event* event = threads[thread_id]->pop();
	if (event->get_start_time() < time) {
		event->incr_os_wait_time(time - event->get_start_time());
	}
	//printf("submitting:   " ); event->print();

	currently_executing_ios.insert(event->get_application_io_id());
	app_id_to_thread_id_mapping[event->get_application_io_id()] = thread_id;

	//printf("dispatching:\t"); event->print();

	ssd->submit(event);
}

void OperatingSystem::setup_follow_up_threads(int thread_id, double current_time) {
	vector<Thread*>& follow_up_threads = threads[thread_id]->get_follow_up_threads();
	if (PRINT_LEVEL >= 1) printf("Switching to new follow up thread\n");
	for (uint i = 0; i < follow_up_threads.size(); i++) {
		Thread* t = follow_up_threads[i];
		t->init(this, current_time);
		int new_id = thread_id_generator++;
		threads[new_id] = t;
	}
	follow_up_threads.clear();
}

void OperatingSystem::register_event_completion(Event* event) {

	bool queue_was_full = currently_executing_ios.size() == MAX_SSD_QUEUE_SIZE;
	currently_executing_ios.erase(event->get_application_io_id());

	//printf("finished:\t"); event->print();
	//printf("queue size:\t%d\n", currently_executing_ios_counter);

	long thread_id = app_id_to_thread_id_mapping[event->get_application_io_id()];
	Thread* thread = threads[thread_id];
	thread->register_event_completion(event);

	if (!event->get_noop() /*&& event->get_event_type() == WRITE*/ && event->get_event_type() != TRIM) {
		num_writes_completed++;
	}

	if (thread->is_finished()) {
		setup_follow_up_threads(thread_id, event->get_current_time());
		threads.erase(thread_id);
		//delete threads[thread_id];
	}

	assert(time <= event->get_current_time() + 1);
	time = max(time, event->get_current_time());

	int thread_with_soonest_event = scheduler->pick(threads);
	if (thread_with_soonest_event != UNDEFINED) {
		dispatch_event(thread_with_soonest_event);
	}

	delete event;
}

/*void OperatingSystem::update_thread_times(double time) {
	map<int, Thread*>::iterator i = threads.begin();
	while (i != threads.end()) {
		Thread* t = (*i).second;
		if (!t->is_finished() && t->get_time() < time) {
			t->set_time(time);
		}
		i++;
	}
}*/

void OperatingSystem::set_num_writes_to_stop_after(long num_writes) {
	NUM_WRITES_TO_STOP_AFTER = num_writes;
}

double OperatingSystem::get_experiment_runtime() const {
	return time;
}

Flexible_Reader* OperatingSystem::create_flexible_reader(vector<Address_Range> ranges) {
	FtlParent* ftl = ssd->get_ftl();
	Flexible_Reader* reader = new Flexible_Reader(*ftl, ranges);
	return reader;
}

