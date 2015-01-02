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
	  idle_time(0),
	  time(0),
	  scheduler(NULL),
	  progress_meter_granularity(20),
	  counter_for_user(0)
{
	ssd->set_operating_system(this);
	thread_id_generator = 0;
	if (OS_SCHEDULER == 0) {
		scheduler = new FIFO_OS_Scheduler();
	} else {
		scheduler = new FAIR_OS_Scheduler();
	}
	//assert(MAX_SSD_QUEUE_SIZE >= SSD_SIZE * PACKAGE_SIZE);
}

void OperatingSystem::set_threads(vector<Thread*> new_threads) {
	for (auto t : historical_threads) {
		delete t;
	}
	historical_threads.clear();
	num_writes_completed = 0;
	threads.clear();
	for (auto t : new_threads) {
		t->init(this, time);
		int new_id = ++thread_id_generator;
		threads[new_id] = t;
		historical_threads.push_back(t);
	}
}

vector<Thread*> OperatingSystem::get_non_finished_threads() {
	vector<Thread*> vec;
	for (auto t : historical_threads) {
		if (t->is_stopped() && !t->is_finished())
		vec.push_back(t);
	}
	return vec;
}

OperatingSystem::~OperatingSystem() {
	delete ssd;
	for (auto t : historical_threads) {
		delete t;
	}
	threads.clear();
	delete scheduler;
}

void OperatingSystem::check_if_stuck(bool no_pending_event, bool queue_is_full) {
	const int idle_limit = 3000000;
	if (idle_time > 100000 && idle_time % 100000 == 0) {
		printf("Idle for %f seconds. No_pending_event=%d  Queue_is_full=%d\n", (double) idle_time / 1000000, no_pending_event, queue_is_full);
		PRINT_LEVEL = 2;
	}
	if (idle_time >= idle_limit) {
		printf("\n");
		if (queue_is_full) {
			fprintf(stderr, "For some reason, application IOs are getting stuck inside the SSD and never making it back to the OS.\n");
			fprintf(stderr, "Normally the SSD invokes the OS's register_event_completion method whenever an application IO completes.\n");
			fprintf(stderr, "This isn't happening, though. Since these IOs are still in the IO queue, we are stuck. We cannot submit any more IOs.\n");
			fprintf(stderr, "The IOs that were submitted but never completed have the following application IDs:\n");
		}
		else if (no_pending_event) {
			fprintf(stderr, "For some reason, the application threads are not submitting IOs.\n");
			fprintf(stderr, "This means we are not making any progress. We are stuck an infinite loop.\n");
			fprintf(stderr, "Hint: try to debug the OS_Scheduler method \"pick\", and try to see why the threads are not submitting IOs.\n");
		}
		for (auto it : currently_executing_ios) {
			printf("%d ", it);
		}

		printf("\n");
		throw;
	}
	idle_time++;
}

void OperatingSystem::print_progess() {
	if ((double)num_writes_completed / NUM_WRITES_TO_STOP_AFTER > (double)counter_for_user / progress_meter_granularity) {
		printf("finished %f%%.\t\tNum writes completed:  %d \n", counter_for_user * 100 / (double)progress_meter_granularity , num_writes_completed);
		/*if (counter_for_user == 90) {
			PRINT_LEVEL = 0;
			VisualTracer::get_instance()->print_horizontally(10000);
			VisualTracer::get_instance()->print_horizontally_with_breaks_last(10000);
		}*/
		counter_for_user++;
	}
}

// The operating system loops until one of two conditions holds:
// 		- no thread
void OperatingSystem::run() {

	bool finished_experiment = false, still_more_work = true;
	do {
		int thread_id = scheduler->pick(threads);
		bool no_pending_event = thread_id == UNDEFINED;
		bool queue_is_full = currently_executing_ios.size() >= MAX_SSD_QUEUE_SIZE;
		int queue_size = currently_executing_ios.size();
		if (no_pending_event || queue_is_full) {
			check_if_stuck(no_pending_event, queue_is_full);
			ssd->progress_since_os_is_waiting();
		}
		else {
			dispatch_event(thread_id);
		}
		print_progess();

		finished_experiment = NUM_WRITES_TO_STOP_AFTER != UNDEFINED && NUM_WRITES_TO_STOP_AFTER <= num_writes_completed;
		still_more_work = currently_executing_ios.size() > 0 || threads.size() > 0;
		//printf("num_writes   %d\n", num_writes_completed);
	} while (!finished_experiment && still_more_work);

	for (auto entry : threads) {
		Thread* t = entry.second;
		t->stop();
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
	printf("Switching to new follow up thread\n");
	//PRINT_LEVEL = 2;
	for (auto t : follow_up_threads) {
		t->init(this, current_time);
		int new_id = ++thread_id_generator;
		threads[new_id] = t;
		historical_threads.push_back(t);
	}
	follow_up_threads.clear();
}

void OperatingSystem::register_event_completion(Event* event) {

	//bool queue_was_full = currently_executing_ios.size() == MAX_SSD_QUEUE_SIZE;
	currently_executing_ios.erase(event->get_application_io_id());

	//printf("finished:\t"); event->print();
	//printf("queue size:\t%d\n", currently_executing_ios_counter);

	long thread_id = app_id_to_thread_id_mapping.at(event->get_application_io_id());
	app_id_to_thread_id_mapping.erase(event->get_application_io_id());
	Thread* thread = threads[thread_id];
	thread->register_event_completion(event);

	if (!event->get_noop() /*&& event->get_event_type() == WRITE*/ && event->get_event_type() != TRIM) {
		num_writes_completed++;
	}

	if (thread->is_finished() && thread->get_num_ongoing_IOs() == 0) {
		setup_follow_up_threads(thread_id, event->get_current_time());
		threads.erase(thread_id);
	}
	if (!event->get_noop()) {
		//assert(time <= event->get_current_time() + 1);
	}
	time = max(time, event->get_current_time());

	int thread_with_soonest_event = scheduler->pick(threads);
	if (thread_with_soonest_event != UNDEFINED) {
		dispatch_event(thread_with_soonest_event);
	}

	delete event;
}


void OperatingSystem::set_num_writes_to_stop_after(long num_writes) {
	NUM_WRITES_TO_STOP_AFTER = num_writes;
}

Flexible_Reader* OperatingSystem::create_flexible_reader(vector<Address_Range> ranges) {
	FtlParent* ftl = ssd->get_ftl();
	Flexible_Reader* reader = new Flexible_Reader(*ftl, ranges);
	return reader;
}

void OperatingSystem::init_threads() {
	for (auto t : threads) {
		Thread* thread = t.second;
		thread->init(this, 0);
	}
}

