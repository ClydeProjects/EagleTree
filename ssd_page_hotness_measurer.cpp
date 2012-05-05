/*
 * ssd_page_hotness_measurer.cpp
 *
 *  Created on: May 5, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <cmath>

using namespace ssd;

#define INTERVAL_LENGTH 1000
#define WEIGHT 0.5


Page_Hotness_Measurer::Page_Hotness_Measurer()
	:	write_current_count(),
		write_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		read_current_count(),
		read_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		current_interval(0)
{}

Page_Hotness_Measurer::~Page_Hotness_Measurer(void) {}

enum write_hotness Page_Hotness_Measurer::get_write_hotness(Address const& page_address) {
	uint addr = page_address.get_linear_address();
	return write_moving_average[addr] >= average_write_hotness ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness Page_Hotness_Measurer::get_read_hotness(Address const& page_address) {
	uint addr = page_address.get_linear_address();
	return read_moving_average[addr] >= average_read_hotness ? READ_HOT : READ_COLD;
}

void Page_Hotness_Measurer::register_event(Event const& event) {
	enum event_type type = event.get_event_type();
	assert(type == WRITE || type == READ);
	double time = event.get_start_time();
	check_if_new_interval(time);
	ulong page_address = event.get_address().get_linear_address();
	if (type == WRITE) {
		write_current_count[page_address]++;
	} else {
		read_current_count[page_address]++;
	}
}

void Page_Hotness_Measurer::check_if_new_interval(double time) {
	int how_many_intervals_into_the_future = trunc((time - current_interval * INTERVAL_LENGTH) / INTERVAL_LENGTH);
	assert(how_many_intervals_into_the_future >= 0);
	if (how_many_intervals_into_the_future == 0) {
		return;
	}

	average_write_hotness = 0;
	average_read_hotness = 0;
	for( uint addr = 0; addr < write_moving_average.size(); addr++  )
	{
	    uint count = write_current_count[addr];
	    write_moving_average[addr] = write_moving_average[addr] * WEIGHT + count * (1 - WEIGHT);
	    write_moving_average[addr] *= pow(WEIGHT, how_many_intervals_into_the_future - 1);
	    average_write_hotness += write_moving_average[addr];

	    count = read_current_count[addr];
	    read_current_count[addr] = read_current_count[addr] * WEIGHT + count * (1 - WEIGHT);
	    read_current_count[addr] *= pow(WEIGHT, how_many_intervals_into_the_future - 1);
	    average_read_hotness += read_current_count[addr];
	}
	average_write_hotness /= write_moving_average.size();
	average_read_hotness /= write_moving_average.size();
}

