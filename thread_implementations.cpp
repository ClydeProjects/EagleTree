/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "ssd.h"
#include "MTRand/mtrand.h"

using namespace ssd;

void Thread::register_event_completion(Event* event) {
	delete event;
}

// =================  Synchronous_Sequential_Writer  =============================

Synchronous_Sequential_Writer::Synchronous_Sequential_Writer(long min_LBA, long max_LBA, int repetitions_num)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  time(1),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num)
{}

Event* Synchronous_Sequential_Writer::issue_next_io() {
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		return new Event(WRITE, min_LBA + counter++, 1, time);
	} else {
		return NULL;
	}
}

void Synchronous_Sequential_Writer::register_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
	if (min_LBA + counter == max_LBA) {
		counter = 0;
		number_of_times_to_repeat--;
	}
	delete event;
}

// =================  Synchronous_Sequential_Writer_And_Reader  =============================

Synchronous_Sequential_Writer_And_Reader::Synchronous_Sequential_Writer_And_Reader(long min_LBA, long max_LBA, int repetitions_num)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  time(1),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num),
	  writing(true)
{}

Event* Synchronous_Sequential_Writer_And_Reader::issue_next_io() {
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		return new Event(writing ? WRITE : READ, min_LBA + counter++, 1, time);
	} else {
		return NULL;
	}
}

void Synchronous_Sequential_Writer_And_Reader::register_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
	if (min_LBA + counter == max_LBA) {
		if (!writing) {
			number_of_times_to_repeat--;
		}
		counter = 0;
		writing = !writing;
	}
	delete event;
}

// =================  Asynchronous_Sequential_Writer  =============================

Asynchronous_Sequential_Writer::Asynchronous_Sequential_Writer(long min_LBA, long max_LBA, int repetitions_num, double start_time)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  offset(0),
	  time(start_time),
	  number_of_times_to_repeat(repetitions_num),
	  finished_round(false)
{}

Event* Asynchronous_Sequential_Writer::issue_next_io() {
	if (number_of_times_to_repeat == 0 || finished_round) {
		return NULL;
	}
	Event* e = new Event(WRITE, min_LBA + offset, 1, time);
	time += 3;
	if (min_LBA + offset++ == max_LBA) {
		finished_round = true;
	}
	return e;
}

void Asynchronous_Sequential_Writer::register_event_completion(Event* event) {
	if (event->get_logical_address() == max_LBA) {
		finished_round = false;
		offset = 0;
		number_of_times_to_repeat--;
		time = event->get_current_time();
	}
}

// =================  Synchronous_Random_Writer  =============================

Synchronous_Random_Writer::Synchronous_Random_Writer(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  time(1),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(num_ios_to_issue)
{
	random_number_generator.seed(randseed);
}

Event* Synchronous_Random_Writer::issue_next_io() {
	if (ready_to_issue_next_write && 0 < number_of_times_to_repeat--) {
		ready_to_issue_next_write = false;
		return new Event(WRITE, min_LBA + random_number_generator() % (max_LBA - min_LBA + 1), 1, time);
	} else {
		return NULL;
	}
}

void Synchronous_Random_Writer::register_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
	delete event;
}


// =================  Asynchronous_Random_Writer  =============================

Asynchronous_Random_Writer::Asynchronous_Random_Writer(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  time(1),
	  number_of_times_to_repeat(num_ios_to_issue)
{
	random_number_generator.seed(randseed);
}

Event* Asynchronous_Random_Writer::issue_next_io() {
	if (0 < number_of_times_to_repeat--) {
		Event* event =  new Event(WRITE, min_LBA + random_number_generator() % (max_LBA - min_LBA + 1), 1, time++);
		return event;
	} else {
		return NULL;
	}
}

