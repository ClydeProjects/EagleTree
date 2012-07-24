/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "ssd.h"
#include "MTRand/mtrand.h"

using namespace ssd;

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
		Event* event =  new Event(WRITE, min_LBA + counter++, 1, time);
		event->set_original_application_io(true);
		return event;
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




Asynchronous_Sequential_Writer::Asynchronous_Sequential_Writer(long min_LBA, long max_LBA, int repetitions_num)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  time(1),
	  number_of_times_to_repeat(repetitions_num)
{}


Event* Asynchronous_Sequential_Writer::issue_next_io() {
	if (number_of_times_to_repeat > 0) {
		Event* event =  new Event(WRITE, min_LBA + counter++, 1, time++);
		event->set_original_application_io(true);
		return event;
	} else {
		return NULL;
	}
}

void Asynchronous_Sequential_Writer::register_event_completion(Event* event) {
	if (min_LBA + counter == max_LBA) {
		counter = 0;
		number_of_times_to_repeat--;
	}
	delete event;
}



Synchronous_Random_Writer::Synchronous_Random_Writer(long min_LBA, long max_LBA, int repetitions_num, ulong randseed)
	: min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  time(1),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num)
{
	random_number_generator.seed(randseed);
}


Event* Synchronous_Random_Writer::issue_next_io() {
	if (ready_to_issue_next_write && counter < number_of_times_to_repeat) {
		ready_to_issue_next_write = false;
		Event* event =  new Event(WRITE, min_LBA + random_number_generator() % (max_LBA - min_LBA + 1), 1, time);
		event->set_original_application_io(true);
		counter++;
		return event;
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
