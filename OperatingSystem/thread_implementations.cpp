/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"

using namespace ssd;

// =================  Thread =============================

void Thread::register_event_completion(Event* event) {
	Address phys = event->get_address();
	Address ra = event->get_replace_address();
	num_pages_in_each_LUN[phys.package][phys.die]++;
	num_writes_to_each_LUN[phys.package][phys.die]++;
	if (ra.valid != NONE) {
		num_pages_in_each_LUN[ra.package][ra.die]--;
	}
	handle_event_completion(event);
}

void Thread::print_thread_stats() {
	printf("printing thread:\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			printf("  %d  %d   %d\n", i, j, num_pages_in_each_LUN[i][j] );
		}
	}
}

// =================  Synchronous_Sequential_Thread  =============================

Synchronous_Sequential_Thread::Synchronous_Sequential_Thread(long min_LBA, long max_LBA, int repetitions_num, event_type type, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num),
	  type(type)
{}

Event* Synchronous_Sequential_Thread::issue_next_io() {
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		return new Event(type, min_LBA + counter++, 1, time);
	} else {
		return NULL;
	}
}

void Synchronous_Sequential_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
	if (min_LBA + counter > max_LBA) {
		counter = 0;
		//StateTracer::print();
		if (--number_of_times_to_repeat == 0) {
			finished = true;
			StateVisualiser::print_page_status();
		}
	}
}

// =================  Asynchronous_Sequential_Writer  =============================

Asynchronous_Sequential_Thread::Asynchronous_Sequential_Thread(long min_LBA, long max_LBA, int repetitions_num, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  offset(0),
	  number_of_times_to_repeat(repetitions_num),
	  finished_round(false),
	  type(type),
	  number_finished(0),
	  time_breaks(time_breaks)
{}

Event* Asynchronous_Sequential_Thread::issue_next_io() {
	if (number_of_times_to_repeat == 0 || finished_round) {
		return NULL;
	}
	Event* e = new Event(type, min_LBA + offset, 1, time);
	time += time_breaks;
	if (min_LBA + offset++ == max_LBA) {
		finished_round = true;
	}
	return e;
}

void Asynchronous_Sequential_Thread::handle_event_completion(Event* event) {
	if (number_finished++ == max_LBA - min_LBA) {
		finished_round = false;
		offset = 0;
		number_of_times_to_repeat--;
		time = event->get_current_time();
		number_finished = 0;
		//StateTracer::print();
		//StatisticsGatherer::get_instance()->print();
	}
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
}

// =================  Synchronous_Random_Writer  =============================

Synchronous_Random_Thread::Synchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  random_number_generator(randseed)
{}

Event* Synchronous_Random_Thread::issue_next_io() {
	if (ready_to_issue_next_write && 0 < number_of_times_to_repeat--) {
		ready_to_issue_next_write = false;
		return new Event(type, min_LBA + random_number_generator() % (max_LBA - min_LBA + 1), 1, time);
	} else {
		return NULL;
	}
}

void Synchronous_Random_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
}


// =================  Asynchronous_Random_Writer  =============================

Asynchronous_Random_Thread::Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  time_breaks(time_breaks),
	  random_number_generator(randseed)
{}

Event* Asynchronous_Random_Thread::issue_next_io() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		event =  new Event(type, min_LBA + random_number_generator() % (max_LBA - min_LBA + 1), 1, time);
		time += time_breaks;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}

	//printf("creating event:  " ); event->print();

	if (event->get_event_type() == READ && event->get_logical_address() == 361) {
		int i = 0;
		i++;
	}

	return event;
}

void Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	//time += event->get_bus_wait_time();
}
