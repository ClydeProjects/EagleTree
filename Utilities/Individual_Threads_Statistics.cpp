/*
 * IndividualThreadsStatistics.cpp
 *
 *  Created on: Jun 11, 2013
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

vector<Thread*> Individual_Threads_Statistics::threads = vector<Thread*>();
vector<string> Individual_Threads_Statistics::thread_names = vector<string>();

void Individual_Threads_Statistics::init() {
	threads.clear();
	thread_names.clear();
}

void Individual_Threads_Statistics::print() {
	for (uint i = 0; i < threads.size(); i++) {
		printf("Statistics for: %s\n", thread_names[i].c_str());
		threads[i]->get_internal_statistics_gatherer()->print();
	}
}

void Individual_Threads_Statistics::register_thread(Thread* t, string name = "") {
	threads.push_back(t);
	thread_names.push_back(name);
}

StatisticsGatherer* Individual_Threads_Statistics::get_stats_for_thread(int index) {
	Thread* t = threads[index];
	return t->get_internal_statistics_gatherer();
}

int Individual_Threads_Statistics::size() {
	return threads.size();
}
