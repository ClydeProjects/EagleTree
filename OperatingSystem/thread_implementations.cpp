/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"
#include <stdlib.h>
using namespace ssd;

// =================  Thread =============================

bool Thread::record_internal_statistics = false;

Thread::Thread() :
		finished(false), time(1), threads_to_start_when_this_thread_finishes(),
		os(NULL), internal_statistics_gatherer(new StatisticsGatherer()),
		external_statistics_gatherer(NULL), num_IOs_executing(0), io_queue(), stopped(false) {}

Thread::~Thread() {
	for (auto t : threads_to_start_when_this_thread_finishes) {
		delete t;
	}
	delete internal_statistics_gatherer;
}

Event* Thread::pop() {
	if (io_queue.size() == 0) {
		return NULL;
	} else {
		Event* next = io_queue.front();
		io_queue.pop();
		return next;
	}
}

Event* Thread::peek() const {
	return io_queue.size() == 0 ? NULL : io_queue.front();
}

void Thread::init(OperatingSystem* new_os, double new_time) {
	os = new_os;
	time = new_time;
	//deque<Event*> empty;
	//swap(empty, submitted_events);
	issue_first_IOs();
	/*for (uint i = 0; i < submitted_events.size() && is_experiment_thread(); i++) {
		Event* e = submitted_events[i];
		if (e != NULL) e->set_experiment_io(true);
	}*/
	//
	//num_IOs_executing += submitted_events.size();
	//queue.insert(queue.begin(), submitted_events.begin(), submitted_events.end());
}

void Thread::register_event_completion(Event* event) {
	//deque<Event*> empty;
	//swap(empty, submitted_events);
	num_IOs_executing--;
	if (record_internal_statistics) {
		internal_statistics_gatherer->register_completed_event(*event);
	}
	if (external_statistics_gatherer != NULL) {
		external_statistics_gatherer->register_completed_event(*event);
	}
	time = event->get_current_time();

	handle_event_completion(event);
	if (num_IOs_executing == 0) {
		handle_no_IOs_left();
		if (num_IOs_executing == 0 && !stopped) {
			finished = true;
		}
	}
}

bool Thread::is_finished() const {
	return finished;
}

bool Thread::can_submit_more() const {
	return io_queue.size() < NUMBER_OF_ADDRESSABLE_PAGES();
}

void Thread::stop() {
	stopped = true;
}

bool Thread::is_stopped() const {
	return stopped;
}

void Thread::submit(Event* event) {
	if (finished || stopped) {
		delete event;
		return;
	}
	event->set_start_time(event->get_current_time());
	io_queue.push(event);
	num_IOs_executing++;
	if (!can_submit_more()) {
		printf("Reached the maximum of events that can be submitted at the same time: %d\n", io_queue.size());
		assert(false);
	}
}

void Thread::set_statistics_gatherer(StatisticsGatherer* new_statistics_gatherer) {
	external_statistics_gatherer = new_statistics_gatherer;
}

// =================  Simple_Thread  =============================

Simple_Thread::Simple_Thread(IO_Pattern* generator, IO_Mode_Generator* mode_gen, int MAX_OUTSTANDING_IOS, long num_IOs)
	: Thread(),
	  MAX_IOS(MAX_OUTSTANDING_IOS),
	  io_gen(generator),
	  io_type_gen(mode_gen),
	  number_of_times_to_repeat(num_IOs),
	  io_size(1)
{
	assert(MAX_IOS > 0);
}

Simple_Thread::Simple_Thread(IO_Pattern* generator, int MAX_IOS, IO_Mode_Generator* mode_gen)
	: Thread(),
	  MAX_IOS(MAX_IOS),
	  io_gen(generator),
	  io_type_gen(mode_gen),
	  io_size(1)
{
	assert(MAX_IOS > 0);
	number_of_times_to_repeat = generator->max_LBA - generator->min_LBA + 1;
}

Simple_Thread::~Simple_Thread() {
	delete io_gen;
	delete io_type_gen;
}

void Simple_Thread::generate_io() {
	while (get_num_ongoing_IOs() < MAX_IOS && number_of_times_to_repeat > 0 && !is_finished() && !is_stopped()) {
		number_of_times_to_repeat--;
		event_type type = io_type_gen->next();
		long logical_addr = io_gen->next();
		Event* e = new Event(type, logical_addr, io_size, get_current_time());
		submit(e);
	}

}

void Simple_Thread::issue_first_IOs() {
	generate_io();
}

void Simple_Thread::handle_event_completion(Event* event) {
	if (number_of_times_to_repeat > 0) {
		generate_io();
	}
}

Random_IO_Pattern_Collision_Free::Random_IO_Pattern_Collision_Free(long min_LBA, long max_LBA, ulong seed) : IO_Pattern(min_LBA, max_LBA), random_number_generator(seed), counter(0) {
	reinit();
};

void Random_IO_Pattern_Collision_Free::reinit() {
	candidates.clear();
	candidates.reserve(max_LBA - min_LBA);
	for (int i = min_LBA; i < max_LBA; i++ ) {
		candidates.push_back(i);
	}
}

int Random_IO_Pattern_Collision_Free::next() {
	bool found = false;
	int index = UNDEFINED;

	do {
		index = random_number_generator() % candidates.size();
		if (candidates[index] != UNDEFINED) {
			found = true;
		}
	} while (!found);
	int lba = candidates[index];
	candidates[index] = UNDEFINED;
	if (candidates.size() == 1 && candidates.front() == UNDEFINED) {
		reinit();
		counter = candidates.size();
	}
	if (++counter >= candidates.size() / 2) {
		vector<int> new_candidates;
		new_candidates.reserve(candidates.size() / 2);
		for (auto i : candidates) {
			if (i != UNDEFINED) {
				new_candidates.push_back(i);
			}
		}
		candidates = new_candidates;
		//printf("size %d\n", candidates.size());
		counter = 0;
	}

	return lba;
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

void Flexible_Reader_Thread::issue_first_IOs() {
	if (flex_reader == NULL) {
		vector<Address_Range> ranges;
		ranges.push_back(Address_Range(min_LBA, max_LBA));
		assert(os != NULL);
		flex_reader = os->create_flexible_reader(ranges);
	}
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		Event* e = flex_reader->read_next(get_current_time());
		submit(e);
	}
}

void Flexible_Reader_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	if (flex_reader->is_finished()) {
		delete flex_reader;
		flex_reader = NULL;
		if (--number_of_times_to_repeat == 0) {
			//finished = true;
			//StateVisualiser::print_page_status();
		}
	}
}

// =================  Collision_Free_Asynchronous_Random_Writer  =============================

/*Collision_Free_Asynchronous_Random_Thread::Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type)
	: Thread(),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  random_number_generator(randseed)
{}

void Collision_Free_Asynchronous_Random_Thread::issue_first_IOs() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		long address;
		do {
			address = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		} while (logical_addresses_submitted.count(address) == 1);
		printf("num events submitted:  %d\n", logical_addresses_submitted.size());
		logical_addresses_submitted.insert(address);
		event =  new Event(type, address, 1, get_current_time());
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	//return event;
}

void Collision_Free_Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	logical_addresses_submitted.erase(event->get_logical_address());
}*/






