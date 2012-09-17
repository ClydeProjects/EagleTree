/*
 * Throughput_Moderator.cpp
 *
 *  Created on: Sep 9, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

/*Throughput_Moderator::Throughput_Moderator()
	: window_size(100),
	  counter(0),
	  window_measurments(window_size, 0),
	  differential(0),
	  diff_sum(0),
	  breaks_counter(window_size),
	  last_average_wait_time(0) {
}

double Throughput_Moderator::register_event_completion(Event const& event) {
	if (breaks_counter-- > 0) {
		return 0;
	}

	double wait_time = event.get_bus_wait_time();
	window_measurments[counter] = wait_time;
	average_wait_time += wait_time / window_size;

	if (counter > 0) {
		diff_sum += window_measurments[counter] - window_measurments[counter - 1];
	}

	if (counter > 0 && counter == window_size - 1) {
		differential = diff_sum / (window_size - 1);

		if (differential < 0.1 && differential > -0.1 && window_size < 500) {
			//window_size *= 2;
		} else if (window_size > 10) {
			//window_size /= 2;
		}

		if (average_wait_time > last_average_wait_time) {
			double other = (last_average_wait_time - average_wait_time) / differential;
			differential = 0;
		}

		printf("differential: %f\n", differential);
		counter = 0;
		diff_sum = 0;
		last_average_wait_time = average_wait_time;
		average_wait_time = 0;
		breaks_counter = window_size * 2;
	} else {
		differential = 0;
		counter++;
	}

	return differential;
}
*/
