#include "../ssd.h"
using namespace ssd;

map<int, long> Queue_Length_Statistics::distribution = map<int, long>();
double Queue_Length_Statistics::last_registry_time = 0;


void Queue_Length_Statistics::init() {
	distribution = map<int, long>();
	last_registry_time = 0;
}

void Queue_Length_Statistics::register_queue_size(int queue_size, double current_time) {
	if (current_time + 0.5 < last_registry_time) {
		return;
	}
	double time_diff = current_time - last_registry_time;
	last_registry_time = current_time;
	distribution[queue_size] += time_diff;
}

void Queue_Length_Statistics::print_avg() {
	map<int, long>::iterator i = distribution.begin();
	long total_time = 0;
	while(i != distribution.end()) {
		total_time += (*i).second;
		i++;
	}
	double avg = 0;
	i = distribution.begin();
	while(i != distribution.end()) {
		avg += (*i).first * ((*i).second / (double)total_time);
		i++;
	}
	printf("avg queue length:  %f\n", avg);
}

void Queue_Length_Statistics::print_distribution() {
	printf("Queue length time distribution\n");
	map<int, long>::iterator i = distribution.begin();
	while( i != distribution.end() ) {
		printf("  %d:\t%d\n", (*i).first, (*i).second);
		i++;

	}
}


