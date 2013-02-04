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

Thread::Thread() :
		finished(false), time(1), threads_to_start_when_this_thread_finishes(), num_ios_finished(0),
		experiment_thread(false), os(NULL), statistics_gatherer(NULL), last_IO_was_null(false), num_IOs_executing(0) {}

Thread::~Thread() {
	for (uint i = 0; i < threads_to_start_when_this_thread_finishes.size(); i++) {
		Thread* t = threads_to_start_when_this_thread_finishes[i];
		if (t != NULL) {
			delete t;
		}
	}
}

deque<Event*> Thread::run() {
	deque<Event*> empty;
	swap(empty, submitted_events);
	if (finished) return empty;
	Event* event = NULL;
	if (!finished) {
		event = issue_next_io();
	}
	if (event != NULL) {
		submitted_events.push_back(event);
	}
	for (uint i = 0; i < submitted_events.size() && is_experiment_thread(); i++) {
		Event* e = submitted_events[i];
		if (e != NULL) e->set_experiment_io(true);
	}
	if (submitted_events.size() == 0) {
		last_IO_was_null = true;
	}
	num_IOs_executing += submitted_events.size();
	//printf("num_IOs_executing:  %d\n", num_IOs_executing);
	return submitted_events;
}

deque<Event*> Thread::register_event_completion(Event* event) {
	deque<Event*> empty;
	swap(empty, submitted_events);
	num_IOs_executing--;
	if (statistics_gatherer != NULL) {
		statistics_gatherer->register_completed_event(*event);
	}
	if (last_IO_was_null) {
		time = event->get_current_time();
	}
	if (!finished) {
		handle_event_completion(event);
	}
	for (uint i = 0; i < submitted_events.size() && is_experiment_thread(); i++) {
		Event* e = submitted_events[i];
		if (e != NULL) {
			e->set_start_time(event->get_current_time());
			if (experiment_thread) e->set_experiment_io(true);
		}
	}
	if (!event->get_noop() && event->get_event_type() != TRIM) {
		num_ios_finished++;
	}
	//printf("num_IOs_executing:  %d\n", num_IOs_executing);
	num_IOs_executing += submitted_events.size();
	assert(num_IOs_executing >= 0);
	return submitted_events;
}

bool Thread::is_finished() {
	return finished && num_IOs_executing == 0;
}

void Thread::print_thread_stats() {
	printf("IOs finished by thread:  %d\n", num_ios_finished);
}

void Thread::set_os(OperatingSystem*  op_sys) {
	os = op_sys;
	op_sys->get_experiment_runtime();
}

void Thread::submit(Event* event) {
	if (event->get_event_type() == TRIM && event->get_logical_address() < 1000) {
		event->print();
	}
	event->set_start_time(event->get_current_time());
	submitted_events.push_front(event);
}

// =================  Simple_Thread  =============================

Simple_Thread::Simple_Thread(IO_Pattern_Generator* generator, int MAX_IOS, event_type type)
	: Thread(),
	  type(type),
	  num_ongoing_IOs(0),
	  MAX_IOS(MAX_IOS),
	  io_gen(generator)
{
	assert(MAX_IOS > 0);
	number_of_times_to_repeat = generator->max_LBA - generator->min_LBA + 1;
}

Simple_Thread::~Simple_Thread() {
	delete io_gen;
}

Event* Simple_Thread::issue_next_io() {
	bool issue = num_ongoing_IOs < MAX_IOS && number_of_times_to_repeat > 0;
	if (issue) {
		num_ongoing_IOs++;
		number_of_times_to_repeat--;
		Event* e = new Event(type, io_gen->next(), 1, time);
		submit(e);
	}
	return NULL;
}

void Simple_Thread::handle_event_completion(Event* event) {
	num_ongoing_IOs--;
	finished = number_of_times_to_repeat == 0 && num_ongoing_IOs == 0;
}

// =================  Flexible_Reader_Thread  =============================

Flexible_Reader_Thread::Flexible_Reader_Thread(long min_LBA, long max_LBA, int repetitions_num)
	: Thread(),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num),
	  flex_reader(NULL)
{}

Event* Flexible_Reader_Thread::issue_next_io() {
	if (flex_reader == NULL) {
		vector<Address_Range> ranges;
		ranges.push_back(Address_Range(min_LBA, max_LBA));
		assert(os != NULL);
		os->get_experiment_runtime();
		flex_reader = os->create_flexible_reader(ranges);
	}
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		return flex_reader->read_next(time);
	} else {
		return NULL;
	}
}

void Flexible_Reader_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	if (flex_reader->is_finished()) {
		delete flex_reader;
		flex_reader = NULL;
		if (--number_of_times_to_repeat == 0) {
			finished = true;
			//StateVisualiser::print_page_status();
		}
	}
}



// =================  Asynchronous_Random_Thread_Reader_Writer  =============================

Asynchronous_Random_Thread_Reader_Writer::Asynchronous_Random_Thread_Reader_Writer(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed)
	: Thread(),
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

// =================  Collision_Free_Asynchronous_Random_Writer  =============================

Collision_Free_Asynchronous_Random_Thread::Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type)
	: Thread(),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
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
