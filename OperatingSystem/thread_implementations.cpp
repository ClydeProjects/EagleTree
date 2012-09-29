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

Thread::~Thread() {
	for (uint i = 0; i < threads_to_start_when_this_thread_finishes.size(); i++) {
		Thread* t = threads_to_start_when_this_thread_finishes[i];
		if (t != NULL) {
			delete t;
		}
	}
}

void Thread::register_event_completion(Event* event) {
	Address phys = event->get_address();
	Address ra = event->get_replace_address();
	handle_event_completion(event);
	if (!event->get_noop() && event->get_event_type() != TRIM) {
		num_ios_finished++;
	}
}

void Thread::print_thread_stats() {
	printf("IOs finished by thread:  %d\n", num_ios_finished);
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
	//time += 1;
	Event* e = new Event(type, min_LBA + offset, 1, time);
	time += 1;
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

Synchronous_Random_Thread::Synchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  random_number_generator(randseed),
	  time_breaks(time_breaks)
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
	time = event->get_current_time() + time_breaks;
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
		time += 1;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}

	//printf("creating event:  " ); event->print();

	return event;
}

void Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	//time += event->get_bus_wait_time();
}

// =================  Asynchronous_Random_Thread_Reader_Writer  =============================

Asynchronous_Random_Thread_Reader_Writer::Asynchronous_Random_Thread_Reader_Writer(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  random_number_generator(randseed)
{}

Event* Asynchronous_Random_Thread_Reader_Writer::issue_next_io() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		event_type type = random_number_generator() % 2 == 0 ? WRITE : READ;
		long lba = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		event =  new Event(type, lba, 1, time);
		//printf("Creating:  %d,  %s   ", numeric_limits<int>::max() - number_of_times_to_repeat, event->get_event_type() == WRITE ? "W" : "R"); event->print();
		time += 1;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
	return event;
}

void Asynchronous_Random_Thread_Reader_Writer::handle_event_completion(Event* event) {}

// =================  Collision_Free_Asynchronous_Random_Writer  =============================

Collision_Free_Asynchronous_Random_Thread::Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  time_breaks(time_breaks),
	  random_number_generator(randseed)
{}

Event* Collision_Free_Asynchronous_Random_Thread::issue_next_io() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		long address;
		do {
			address = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		} while (logical_addresses_submitted.count(address) == 1);
		printf("num events submitted:  %d\n", logical_addresses_submitted.size());
		logical_addresses_submitted.insert(address);
		event =  new Event(type, address, 1, time);
		time += time_breaks;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
	return event;
}

void Collision_Free_Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	logical_addresses_submitted.erase(event->get_logical_address());
}
