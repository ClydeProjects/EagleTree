/*
 * external_sort.cpp
 *
 *  Created on: Aug 2, 2012
 *      Author: niv
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"

using namespace ssd;

External_Sort::External_Sort(long relation_min_LBA, long relation_max_LBA, long RAM_available,
		long free_space_min_LBA, long free_space_max_LBA) :
		Thread(), relation_min_LBA(relation_min_LBA), relation_max_LBA(relation_max_LBA), RAM_available(RAM_available),
		free_space_min_LBA(free_space_min_LBA), free_space_max_LBA(free_space_max_LBA),
		cursor(0), counter(0), number_finished(0), phase(FIRST_PHASE_READ)
{
	assert(relation_min_LBA < relation_max_LBA);
	assert(free_space_min_LBA < free_space_max_LBA);
	assert(RAM_available < relation_max_LBA - relation_min_LBA);
	assert(free_space_max_LBA - free_space_min_LBA >= relation_max_LBA - relation_min_LBA);
	long relation_size = relation_max_LBA - relation_min_LBA;
	double num_partitions_double = relation_size / (double)RAM_available;
	num_partitions = floor(num_partitions_double + 0.5);
	num_pages_in_last_partition = relation_size - (num_partitions - 1) * RAM_available;
}

void External_Sort::issue_first_IOs() {
	Event* e;
	if (phase == FIRST_PHASE_READ || phase == FIRST_PHASE_WRITE) {
		e =  execute_first_phase();
	} else if (phase == SECOND_PHASE) {
		e = execute_second_phase();
	} else if (phase == THIRD_PHASE) {
		e =  execute_third_phase();
	} else /* if (phase == FINISHED) */ {
		return;
	}
	submit(e);
}

Event* External_Sort::execute_first_phase() {
	long lba_start = phase == FIRST_PHASE_READ ? relation_min_LBA : free_space_min_LBA;
	if (counter++ < RAM_available) {
		enum event_type op_type = phase == FIRST_PHASE_READ ? READ : WRITE;
		return new Event(op_type, lba_start + cursor++, 1, get_current_time());
	} else {
		return NULL;
	}
}

Event* External_Sort::execute_second_phase() {
	Event* io = NULL;
	if (can_start_next_read) {
		can_start_next_read = false;
		long current_lba = counter++ * RAM_available + cursor;
		io = new Event(READ, current_lba, 1, get_current_time());
		long next_lba = counter * RAM_available + cursor;
		if (next_lba > relation_max_LBA - relation_min_LBA + free_space_min_LBA) {
			counter = 0;
			cursor++;
		}
	}
	return io;
}

Event* External_Sort::execute_third_phase() {
	if (cursor <= relation_max_LBA - relation_min_LBA + free_space_min_LBA) {
		return new Event(TRIM, cursor++, 1, get_current_time());
	}
	return NULL;
}

void External_Sort::handle_event_completion(Event* event) {
	number_finished++;
	if (phase == FIRST_PHASE_READ && number_finished == RAM_available) {
		counter = number_finished = 0;
		phase = FIRST_PHASE_WRITE;
		cursor -= RAM_available;
		//time = event->get_current_time();
	}
	else if (phase == FIRST_PHASE_WRITE && number_finished == RAM_available && event->get_logical_address() > free_space_min_LBA + RAM_available * (num_partitions - 1)) {
		counter = number_finished = 0;
		phase = SECOND_PHASE;
		//time = event->get_current_time();
	}
	else if (phase == FIRST_PHASE_WRITE && number_finished == RAM_available) {
		counter = number_finished = 0;
		phase = FIRST_PHASE_READ;
		//time = event->get_current_time();
		can_start_next_read = true;
	}
	else if (phase == SECOND_PHASE && number_finished == relation_max_LBA - relation_min_LBA + 1) {
		phase = THIRD_PHASE;
		cursor = free_space_min_LBA;
		//time = event->get_current_time() + 2;
		number_finished = 0;
	}
	else if (phase == SECOND_PHASE) {
		can_start_next_read = true;
		//time = event->get_current_time();
	}
	else if (phase == THIRD_PHASE && number_finished == relation_max_LBA - relation_min_LBA + 1) {
		phase = FINISHED;
		//finished = true;
	}
}

