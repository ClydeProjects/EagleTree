/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

int FIFO_OS_Scheduler::pick(map<int, Thread*> const& threads) {
	double soonest_event_time = INFINITE;
	int thread_id_with_soonest_event = UNDEFINED;
	map<int, Thread*>::const_iterator i = threads.begin();
	for (; i != threads.end(); i++) {
		int id = (*i).first;
		Thread* t = (*i).second;
		Event* e = t->peek();
		if (e != NULL && e->get_current_time() < soonest_event_time) {
			soonest_event_time = e->get_current_time();
			thread_id_with_soonest_event = id;
		}
	}
	return thread_id_with_soonest_event;
}

int FAIR_OS_Scheduler::pick(map<int, Thread*> const& threads) {
	map<int, Thread*>::const_iterator i = threads.find(last_id);
	bool found = false;
	uint num_tried = 0;
	while (!found && num_tried < threads.size()) {
		num_tried++;
		i++;
		if (i == threads.end()) {
			i = threads.begin();
		}
		last_id = (*i).first;
		Thread* t = (*i).second;
		if (t->peek() != NULL) {
			found = true;
		}
	}
	return !found ? UNDEFINED : last_id;
}
