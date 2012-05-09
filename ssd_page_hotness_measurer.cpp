/*
 * ssd_page_hotness_measurer.cpp
 *
 *  Created on: May 5, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <cmath>

using namespace ssd;

#define INTERVAL_LENGTH 500
#define WEIGHT 0.5


Page_Hotness_Measurer::Page_Hotness_Measurer()
	:	write_current_count(),
		write_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		read_current_count(),
		read_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		current_interval(0),
		num_wcrh_pages_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		num_wcrc_pages_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		current_reads_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		average_reads_per_die(SSD_SIZE, std::vector<double>(PACKAGE_SIZE, 0)),
		writes_counter(0),
		reads_counter(0)
{}

Page_Hotness_Measurer::~Page_Hotness_Measurer(void) {}

enum write_hotness Page_Hotness_Measurer::get_write_hotness(unsigned long page_address) const {
	return write_moving_average[page_address] >= average_write_hotness ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness Page_Hotness_Measurer::get_read_hotness(unsigned long page_address) const {
	return read_moving_average[page_address] >= average_read_hotness ? READ_HOT : READ_COLD;
}

Address Page_Hotness_Measurer::get_die_with_least_wcrh() const {
	uint package;
	uint die;
	double min = PLANE_SIZE * BLOCK_SIZE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (min >= num_wcrh_pages_per_die[i][j]) {
				min = num_wcrh_pages_per_die[i][j];
				package = i;
				die = j;
			}
		}
	}
	return Address(package, die, 0,0,0, DIE);
}

Address Page_Hotness_Measurer::get_die_with_least_wcrc() const {
	uint package;
	uint die;
	double min = PLANE_SIZE * BLOCK_SIZE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			//printf("%d\n", num_wcrc_pages_per_die[i][j]);
			if (min >= num_wcrc_pages_per_die[i][j]) {
				min = num_wcrc_pages_per_die[i][j];
				package = i;
				die = j;
			}
		}
	}
	return Address(package, die, 0,0,0, DIE);
}

void Page_Hotness_Measurer::register_event(Event const& event) {
	enum event_type type = event.get_event_type();
	assert(type == WRITE || type == READ_COMMAND);
	double time = event.get_start_time() + event.get_time_taken();
	ulong page_address = event.get_logical_address();
	if (type == WRITE) {
		write_current_count[page_address]++;
		if (++writes_counter == INTERVAL_LENGTH) {
			writes_counter = 0;
			start_new_interval_writes();
		}
	} else if (type == READ_COMMAND) {
		current_reads_per_die[event.get_address().package][event.get_address().die]++;
		read_current_count[page_address]++;
		if (++reads_counter == INTERVAL_LENGTH) {
			reads_counter = 0;
			start_new_interval_reads();
		}
	}
}

void Page_Hotness_Measurer::start_new_interval_writes() {
	average_write_hotness = 0;
	for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE; addr++  )
	{
	    uint count = write_current_count[addr];
	    write_moving_average[addr] = write_moving_average[addr] * WEIGHT + count * (1 - WEIGHT);
	    average_write_hotness += write_moving_average[addr];
	}
	average_write_hotness /= NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			num_wcrc_pages_per_die[i][j] = 0;
			num_wcrh_pages_per_die[i][j] = 0;
		}
	}

	for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE; addr++  )
	{
		if (get_write_hotness(addr) == WRITE_COLD) {
			Address a = Address(addr, PAGE);
			if (get_read_hotness(addr) == READ_COLD) {
				num_wcrc_pages_per_die[a.package][a.die]++;
			} else {
				num_wcrh_pages_per_die[a.package][a.die]++;
			}
		}
	}
}

void Page_Hotness_Measurer::start_new_interval_reads() {
	average_read_hotness = 0;
	for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE; addr++  )
	{
	    uint count = read_current_count[addr];
	    read_moving_average[addr] = read_moving_average[addr] * WEIGHT + count * (1 - WEIGHT);
	    average_read_hotness += read_moving_average[addr];
	}
	average_read_hotness /= NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			average_reads_per_die[i][j] = average_reads_per_die[i][j] * WEIGHT + current_reads_per_die[i][j] * (1 - WEIGHT);
			current_reads_per_die[i][j] = 0;
		}
	}
}

