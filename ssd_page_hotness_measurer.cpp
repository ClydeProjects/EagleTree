/*
 * ssd_page_hotness_measurer.cpp
 *
 *  Created on: May 5, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <cmath>
#include <iostream>
#include "bloom_filter.hpp"

using namespace ssd;

// Interface constructor/destructor
//Page_Hotness_Measurer::Page_Hotness_Measurer() {}
//Page_Hotness_Measurer::~Page_Hotness_Measurer(void) {}

/*
 * Simple hotness measurer
 * ----------------------------------------------------------------------------------
 * Naïve implementation
 */

#define INTERVAL_LENGTH 500
#define WEIGHT 0.5

Simple_Page_Hotness_Measurer::Simple_Page_Hotness_Measurer()
	:	write_current_count(),
		write_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		read_current_count(),
		read_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE, 0),
		current_interval(0),
		num_wcrh_pages_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		num_wcrc_pages_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		average_reads_per_die(SSD_SIZE, std::vector<double>(PACKAGE_SIZE, 0)),
		current_reads_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		writes_counter(0),
		reads_counter(0)
{}

Simple_Page_Hotness_Measurer::~Simple_Page_Hotness_Measurer(void) {}

enum write_hotness Simple_Page_Hotness_Measurer::get_write_hotness(unsigned long page_address) const {
	return write_moving_average[page_address] >= average_write_hotness ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness Simple_Page_Hotness_Measurer::get_read_hotness(unsigned long page_address) const {
	return read_moving_average[page_address] >= average_read_hotness ? READ_HOT : READ_COLD;
}

/*Address Simple_Page_Hotness_Measurer::get_die_with_least_wcrh() const {
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
}*/

/*Address Simple_Page_Hotness_Measurer::get_die_with_least_wcrc() const {
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
}*/

Address Simple_Page_Hotness_Measurer::get_die_with_least_WC(enum read_hotness rh) const {
	uint package;
	uint die;
	std::vector<std::vector<uint> > num_such_pages_per_die;
	if (rh == READ_COLD) {
		num_such_pages_per_die = num_wcrc_pages_per_die;
	} else if (rh == READ_HOT) {
		num_such_pages_per_die = num_wcrh_pages_per_die;
	}

	double min = PLANE_SIZE * BLOCK_SIZE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			//printf("%d\n", num_wcrc_pages_per_die[i][j]);
			if (min >= num_such_pages_per_die[i][j]) {
				min = num_such_pages_per_die[i][j];
				package = i;
				die = j;
			}
		}
	}
	return Address(package, die, 0,0,0, DIE);
}

void Simple_Page_Hotness_Measurer::register_event(Event const& event) {
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

void Simple_Page_Hotness_Measurer::start_new_interval_writes() {
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

void Simple_Page_Hotness_Measurer::start_new_interval_reads() {
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

/*
 * Bloom Filter based hotness measurer
 * ----------------------------------------------------------------------------------
 * By Martin Kjær Svendsen Based on work by Park and Du
 */

BloomFilter_Page_Hotness_Measurer::BloomFilter_Page_Hotness_Measurer(unsigned int numBloomFilters, unsigned int bloomFilterSize, unsigned int decayTime)
	:	oldest_BF(0),                 // Bloom filter with oldest data; next to be reset
	 	BF_read_pos(0),               // Read BF Cursor position
	 	BF_write_pos(0),              // Write BF Cursor position
		read_counter(0),              // Read command counter
		write_counter(0),             // Write command counter
		V(numBloomFilters),           // Number of bloom filters
		M(bloomFilterSize),           // Size of each bloom filter
//		K(2),                         // Number of hash functions
		T(decayTime),                 // Number of I/Os before decay
		hotness_threshold(1),         // Threshold value for considering a given page hot
		read_counter_window_size(128)
{
	bloom_parameters parameters;
	parameters.projected_element_count    = T;
    parameters.false_positive_probability = 0.01;
	//parameters.random_seed                = ++random_seed;
	if (!parameters) std::cout << "Error - Invalid set of bloom filter parameters!" << std::endl;

	printf("Chosen false positive probabality: %f\nChosen projected element count: %u\n", parameters.false_positive_probability, parameters.projected_element_count);
	parameters.compute_optimal_parameters();
	printf("bloom_filter optimal params:\nNumber of hashes: %d\nTable size: %d bits (%d bytes)\n", parameters.optimal_parameters.number_of_hashes, parameters.optimal_parameters.table_size, parameters.optimal_parameters.table_size / 8);

	read_bloom.resize(V, bloom_filter(parameters));
	write_bloom.resize(V, bloom_filter(parameters));

	// Initialize 2D vector package_die_stats indexed by [package][die], used for keeping track of LUN usage statistics
	std::vector<Die_Stats> die_stats = std::vector<Die_Stats>(PACKAGE_SIZE, Die_Stats(parameters)); // !! NOTE TO SELF: Parameters needs to be tuned for this application
	std::vector< std::vector<Die_Stats> > package_die_stats(SSD_SIZE, die_stats);
}

BloomFilter_Page_Hotness_Measurer::~BloomFilter_Page_Hotness_Measurer(void) {}

enum write_hotness BloomFilter_Page_Hotness_Measurer::get_write_hotness(unsigned long page_address) const {
	return (get_hot_data_index(write_bloom, page_address) >= hotness_threshold) ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness BloomFilter_Page_Hotness_Measurer::get_read_hotness(unsigned long page_address) const {
	return (get_hot_data_index(read_bloom, page_address) >= hotness_threshold) ? READ_HOT : READ_COLD;
}

Address BloomFilter_Page_Hotness_Measurer::get_die_with_least_WC(enum read_hotness rh) const {
	// To be implemented.
	return Address(0,0,0,0,0,DIE);
/*	uint package;
	uint die;
	std::vector<std::vector<uint> > num_such_pages_per_die;
	if (rh == READ_COLD) {
		num_such_pages_per_die = num_wcrc_pages_per_die;
	} else if (rh == READ_HOT) {
		num_such_pages_per_die = num_wcrh_pages_per_die;
	}

	double min = PLANE_SIZE * BLOCK_SIZE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			//printf("%d\n", num_wcrc_pages_per_die[i][j]);
			if (min >= num_such_pages_per_die[i][j]) {
				min = num_such_pages_per_die[i][j];
				package = i;
				die = j;
			}
		}
	}

	return Address(package, die, 0,0,0, DIE); */
}

void BloomFilter_Page_Hotness_Measurer::register_event(Event const& event) {
	// Fetch page address information and type (read/write) from event
	enum event_type type = event.get_event_type();
	assert(type == WRITE || type == READ_COMMAND);
	ulong page_address = event.get_logical_address();
	Address invalidated_address = event.get_replace_address(); // The physical address of page being invalidated
	Address physical_address = event.get_address(); // The physical address of page written
	Die_Stats& current_die_stats = package_die_stats[physical_address.package][physical_address.die];

	// Set references to variables corresponding to chosen event type (read/write)
	hot_bloom_filter& filter = (type == WRITE ? write_bloom : read_bloom);
	unsigned int& counter = (type == WRITE ? write_counter : read_counter);
	unsigned int& pos = (type == WRITE ? BF_write_pos : BF_read_pos);
	unsigned int startPos = pos;

	// Decay: Reset oldest filter if the time has come
	if (++counter >= T) {
		filter[oldest_BF].clear();
		oldest_BF = (oldest_BF + 1) % V;
		counter = 0;
	}

	// Find a filter where address is not present (if any), and insert
	do {
		pos = (pos + 1) % V;
		if (filter[pos].contains(page_address)) continue; // Address already in this filter, try next
		filter[pos].insert(page_address); // Address not in filter, insert and stop
		break;
	} while (pos != startPos);

	bool address_write_hot = (get_write_hotness(page_address) == WRITE_HOT);
	bool address_read_hot  = (get_read_hotness(page_address) == READ_HOT);

	// Keep track of live pages per LUN: Increment counter when page is written to LUN, decrement when page is invalidated in LUN
	if (type == WRITE) {
		current_die_stats.live_pages += 1;

		// If WH data has been moved to new LUN, increase its WH counter
		if (address_write_hot) {
			current_die_stats.unique_wh_encountered += 1;
			current_die_stats.wh_counted_already.insert(page_address);
		}

		// If an address is being invalidated, decrement counter of corresponding LUN
		if (event.get_replace_address().valid != NONE) {
			Die_Stats& invalidated_die_stats = package_die_stats[invalidated_address.package][invalidated_address.die];
			invalidated_die_stats.live_pages -= 1;

			// If WH data is removed from a LUN, and it has been previously been counted as WH, decrease counter to undo count
			if (address_write_hot && current_die_stats.wh_counted_already.contains(page_address)) {
				current_die_stats.unique_wh_encountered -= 1;
				// Here it would have been nice to remove physical address from unique_wh_encountered bloom filter, but that is not possible.
			}
		}
	}

	// Keep track of reads targeting WC pages per LUN
	if (type == READ) {
		current_die_stats.reads += 1;

		// If end of window is reached, reset
		if (current_die_stats.reads > read_counter_window_size) {
			current_die_stats.reads = 0;
			current_die_stats.reads_targeting_wc_pages_previous_window = current_die_stats.reads_targeting_wc_pages;
			current_die_stats.reads_targeting_wc_pages = 0;
		}

		// Count reads targeting WC pages
		if (!get_write_hotness(page_address)) current_die_stats.reads_targeting_wc_pages += 1;
	}

	// Keep track of unique WH per LUN
	if (type == WRITE) {
		current_die_stats.writes += 1;

		// If end of window is reached, reset
		if (current_die_stats.writes > write_counter_window_size) {
			current_die_stats.writes = 0;
			current_die_stats.unique_wh_encountered_previous_window = current_die_stats.unique_wh_encountered;
			current_die_stats.unique_wh_encountered = 0;
		}

		// Count unique WHs
		if (address_write_hot && !current_die_stats.wh_counted_already.contains(page_address)) {
			current_die_stats.wh_counted_already.insert(page_address);
			current_die_stats.unique_wh_encountered += 1;
		}
	}
}

double BloomFilter_Page_Hotness_Measurer::get_hot_data_index(hot_bloom_filter const& filter, unsigned long page_address) const {
	double result = 0;
	double stepSize = 2 / (double) V;
	unsigned int pos = oldest_BF;
	unsigned int newness = 0;

	// Iterate though BFs from oldest to newest, adding recency weight to result if address in BF
	do {
		newness++;
		if (filter[pos].contains(page_address)) result += (newness * stepSize);
		pos = (pos + 1) % V;
	} while (pos != oldest_BF);

	return result;
}

