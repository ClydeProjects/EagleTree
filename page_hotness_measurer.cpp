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

#define WEIGHT 0.5

Simple_Page_Hotness_Measurer::Simple_Page_Hotness_Measurer()
	:	write_current_count(),
		write_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE, 0),
		read_current_count(),
		read_moving_average(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE, 0),
		current_interval(0),
		writes_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		reads_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		average_reads_per_die(SSD_SIZE, std::vector<double>(PACKAGE_SIZE, 0)),
		current_reads_per_die(SSD_SIZE, std::vector<uint>(PACKAGE_SIZE, 0)),
		writes_counter(0),
		reads_counter(0),
		WINDOW_LENGTH(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		KICK_START(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 2)
{}

Simple_Page_Hotness_Measurer::~Simple_Page_Hotness_Measurer(void) {}

enum write_hotness Simple_Page_Hotness_Measurer::get_write_hotness(ulong page_address) const {
	return writes_counter < KICK_START && reads_counter < KICK_START ? WRITE_HOT :
			write_moving_average[page_address] >= average_write_hotness ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness Simple_Page_Hotness_Measurer::get_read_hotness(ulong page_address) const {
	return writes_counter < KICK_START && reads_counter < KICK_START ? READ_HOT :
			read_moving_average[page_address] >= average_read_hotness ? READ_HOT : READ_COLD;
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

double compute_average(vector<vector<uint> > s) {
	double average = 0;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			average += s[i][j];
		}
	}
	average /= s.size() * s[0].size();
	return average;
}

Address Simple_Page_Hotness_Measurer::get_best_target_die_for_WC(enum read_hotness rh) const {
	int package = UNDEFINED;
	int die = UNDEFINED;
	/*vector<vector<uint> > num_such_pages_per_die;
	if (rh == READ_COLD) {
		num_such_pages_per_die = reads_per_die;
	} else if (rh == READ_HOT) {
		num_such_pages_per_die = writes_per_die;
	}*/

	//double average_reads_per_LUN = compute_average(reads_per_die);
	double average_writes_per_LUN = compute_average(writes_per_die);

	double min_product = numeric_limits<double>::max();
	double max_product = numeric_limits<double>::min();
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double writes = writes_per_die[i][j];
			if (writes > average_writes_per_LUN) {
				double reads = reads_per_die[i][j];
				double product = reads * writes;
				if (rh == READ_HOT && product < min_product) {
					min_product = product;
					package = i;
					die = j;
				}
				else if (rh == READ_COLD && product > max_product) {
					max_product = product;
					package = i;
					die = j;
				}
			}
		}
	}


	return Address(package, die, 0,0,0, DIE);
}

void Simple_Page_Hotness_Measurer::register_event(Event const& event) {
	enum event_type type = event.get_event_type();
	assert(type == WRITE || type == READ_COMMAND);

	double time = event.get_current_time();

	ulong page_address = event.get_logical_address();
	Address phys_addr = event.get_address();
	if (type == WRITE) {
		write_current_count[page_address]++;
		if (++writes_counter % WINDOW_LENGTH == 0) {
			start_new_interval_writes();
		}
		if (writes_counter == KICK_START && PRINT_LEVEL >= 1) {
			printf("Start read temperature Identification\n");
		}
		writes_per_die[phys_addr.package][phys_addr.die]++;
	} else if (type == READ_COMMAND) {
		current_reads_per_die[event.get_address().package][event.get_address().die]++;
		read_current_count[page_address]++;
		if (++reads_counter % WINDOW_LENGTH == 0) {
			start_new_interval_reads();
		}
		if (reads_counter == KICK_START && PRINT_LEVEL >= 1) {
			printf("Start write temperature Identification\n");
		}
		reads_per_die[phys_addr.package][phys_addr.die]++;
	}
}

void Simple_Page_Hotness_Measurer::start_new_interval_writes() {
	average_write_hotness = 0;
	for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE; addr++  )
	{
	    uint count = write_current_count[addr];
	    write_moving_average[addr] = write_moving_average[addr] * WEIGHT + count * (1 - WEIGHT);
	    average_write_hotness += write_moving_average[addr];
	}
	average_write_hotness /= NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			writes_per_die[i][j] = writes_per_die[i][j] * 0.5;
		}
	}

	/*for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE; addr++  )
	{
		if (get_write_hotness(addr) == WRITE_COLD) {
			Address a = Address(addr, PAGE);
			if (get_read_hotness(addr) == READ_COLD) {
				num_wcrc_pages_per_die[a.package][a.die]++;
			} else {
				num_wcrh_pages_per_die[a.package][a.die]++;
			}
		}
	}*/
}

void Simple_Page_Hotness_Measurer::start_new_interval_reads() {
	average_read_hotness = 0;
	for( uint addr = 0; addr < NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE; addr++  )
	{
	    uint count = read_current_count[addr];
	    read_moving_average[addr] = read_moving_average[addr] * WEIGHT + count * (1 - WEIGHT);
	    average_read_hotness += read_moving_average[addr];
	}
	average_read_hotness /= NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;

	/*for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			average_reads_per_die[i][j] = average_reads_per_die[i][j] * WEIGHT + current_reads_per_die[i][j] * (1 - WEIGHT);
			current_reads_per_die[i][j] = 0;
		}
	}*/

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			reads_per_die[i][j] = reads_per_die[i][j] * 0.5;
		}
	}

}

/* ==================================================================================
 * Bloom Filter based hotness measurer
 * ----------------------------------------------------------------------------------
 * By Martin Kjær Svendsen Based on work by Park and Du
 * ================================================================================== */

BloomFilter_Page_Hotness_Measurer::BloomFilter_Page_Hotness_Measurer(uint num_bloom_filters, uint bloom_filter_size, uint IOs_before_decay, bool preheat)
	:	read_oldest_BF(0),						// Read bloom filter with oldest data; next to be reset
	 	write_oldest_BF(0),						// Write bloom filter with oldest data; next to be reset
		read_counter(0),						// Read command counter
		write_counter(0),						// Write command counter
		num_bloom_filters(num_bloom_filters),	// Number of bloom filters
		bloom_filter_size(bloom_filter_size),	// Size of each bloom filter
		IOs_before_decay(IOs_before_decay),		// Number of I/Os before decay
		hotness_threshold(1),					// Threshold value for considering a given page hot
		read_counter_window_size(128),
		write_counter_window_size(128)
{
	bloom_parameters parameters;
	parameters.projected_element_count    = IOs_before_decay;
    parameters.false_positive_probability = 0.01;
	//parameters.random_seed                = ++random_seed;
	if (!parameters) std::cout << "Error - Invalid set of bloom filter parameters!" << std::endl;

	if (PRINT_LEVEL >= 1) printf("Chosen false positive probability: %f\nChosen projected element count: %llu\n", parameters.false_positive_probability, parameters.projected_element_count);
	parameters.compute_optimal_parameters();
	if (PRINT_LEVEL >= 1) printf("bloom_filter optimal parameters:\nNumber of hashes: %d\nTable size: %llu bits (%llu bytes)\n", parameters.optimal_parameters.number_of_hashes, parameters.optimal_parameters.table_size, parameters.optimal_parameters.table_size / 8);

	read_bloom.resize(num_bloom_filters, bloom_filter(parameters));
	write_bloom.resize(num_bloom_filters, bloom_filter(parameters));

	// Initialize 2D vector package_die_stats indexed by [package][die], used for keeping track of LUN usage statistics
	package_die_stats.resize(SSD_SIZE);
	for (uint ssd_size = 0; ssd_size < SSD_SIZE; ssd_size++) {
		package_die_stats.reserve(PACKAGE_SIZE);
		for (uint package_size = 0; package_size < PACKAGE_SIZE; package_size++) {
			package_die_stats[ssd_size].push_back(Die_Stats(parameters, read_counter_window_size, write_counter_window_size)); // !! NOTE TO SELF: Parameters needs to be tuned for this application
		}
	}

	if (preheat) heat_all_addresses();
}

BloomFilter_Page_Hotness_Measurer::~BloomFilter_Page_Hotness_Measurer(void) {}

enum write_hotness BloomFilter_Page_Hotness_Measurer::get_write_hotness(ulong page_address) const {
	return (get_hot_data_index(WRITE, page_address) >= hotness_threshold) ? WRITE_HOT : WRITE_COLD;
}

enum read_hotness BloomFilter_Page_Hotness_Measurer::get_read_hotness(ulong page_address) const {
	return (get_hot_data_index(READ, page_address) >= hotness_threshold) ? READ_HOT : READ_COLD;
}

void BloomFilter_Page_Hotness_Measurer::print_die_stats() const {
	for (uint package = 0; package < SSD_SIZE; package++) {
		for (uint die = 0; die < PACKAGE_SIZE; die++) {
			printf("=== Package: %d, Die: %d =================================\n", package, die);
			package_die_stats[package][die].print();
		}
	}
	StateVisualiser::print_page_status(); // DEBUG
}

// Looks at all dies having less than the average number of WC pages:
// - If placement of WCRH are wanted, the die with least reads per WC page are chosen
// - If placement of WCRC are wanted, the die with most reads per WC page are chosen
Address BloomFilter_Page_Hotness_Measurer::get_best_target_die_for_WC(enum read_hotness rh) const {
	// UNOPTIMIZED: Total WC pages computed by a linear pass through all dies; It could easily be kept track of incrementally
	uint total_wc_pages = 0;
	for (uint package = 0; package < SSD_SIZE; package++) {
		for (uint die = 0; die < PACKAGE_SIZE; die++) {
			total_wc_pages += package_die_stats[package][die].get_wc_pages();
			//printf("%d + ", package_die_stats[package][die].get_wc_pages());
		}
	}
	double average_wc_pages = (double) total_wc_pages / (SSD_SIZE * PACKAGE_SIZE);
	//printf(" = %u\nAverage: %f per die.\n", total_wc_pages, average_wc_pages);

	int best_candidate_package = -1;
	int best_candidate_die = -1;
	double best_num_reads_per_wc = (rh == READ_HOT ? numeric_limits<double>::max() : -numeric_limits<double>::max());

	// Iterate though all dies with less than average WC pages, and find the one thats best for inserting WCRC or WCRH pages, depending on rh parameter
	for (uint package = 0; package < SSD_SIZE; package++) {
		for (uint die = 0; die < PACKAGE_SIZE; die++) {

			if (package_die_stats[package][die].get_wc_pages() <= average_wc_pages) {
				if (package_die_stats[package][die].get_wc_pages() == 0) return Address(package, die, 0,0,0, DIE); // If die has zero WC pages it is chosen as the target with no further search
				double reads_per_wc = (double) package_die_stats[package][die].get_reads_targeting_wc_pages() / package_die_stats[package][die].get_wc_pages();
				if ((rh == READ_HOT  && reads_per_wc < best_num_reads_per_wc) ||
					(rh == READ_COLD && reads_per_wc > best_num_reads_per_wc))
				{
					best_num_reads_per_wc = reads_per_wc;
					best_candidate_package = package;
					best_candidate_die = die;
				}
			}
		}
	}

	assert(best_candidate_package != -1 && best_candidate_die != -1);
	return Address(best_candidate_package, best_candidate_die, 0,0,0, DIE);
}

void BloomFilter_Page_Hotness_Measurer::register_event(Event const& event) {
	// Fetch page address information and type (read/write) from event
	enum event_type type = event.get_event_type();
	assert(type == WRITE || type == READ_COMMAND || type == COPY_BACK);
	ulong page_address = event.get_logical_address();
	Address invalidated_address = event.get_replace_address(); // The physical address of page being invalidated
	Address physical_address = event.get_address(); // The physical address of page written
	Die_Stats& current_die_stats = package_die_stats[physical_address.package][physical_address.die];

	// Set references to variables corresponding to chosen event type (read/write)
	hot_bloom_filter& filter = (type == WRITE ? write_bloom : read_bloom);
	uint& counter = (type == WRITE ? write_counter : read_counter);
	uint& oldest_BF = (type == WRITE ? write_oldest_BF : read_oldest_BF);
	uint& pos = (type == WRITE ? write_oldest_BF : read_oldest_BF);
	uint startPos = pos;

	if (event.is_original_application_io()) {
		// Decay: Reset oldest filter if the time has come
		if (++counter >= IOs_before_decay) {
			filter[oldest_BF].clear();
			oldest_BF = (oldest_BF + 1) % num_bloom_filters;
			counter = 0;
		}

		// Find a filter where address is not present (if any), starting from newest, and insert
		do {
			pos = (pos + num_bloom_filters - 1) % num_bloom_filters; // Move backwards from newest to oldest in a round-robin fashion
			if (filter[pos].contains(page_address)) continue; // Address already in this filter, try next
			filter[pos].insert(page_address); // Address not in filter, insert and stop
			break;
		} while (pos != startPos);

		if (type == WRITE) {
			// If end of window is reached, reset
			if (current_die_stats.writes > write_counter_window_size) {
				current_die_stats.writes = 0;
				current_die_stats.unique_wh_encountered_previous_window = current_die_stats.unique_wh_encountered;
				current_die_stats.unique_wh_encountered = 0;
				current_die_stats.wh_counted_already.clear();
			}
			current_die_stats.writes += 1;
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
	}

	bool address_write_hot = (get_write_hotness(page_address) == WRITE_HOT);
	bool address_read_hot  = (get_read_hotness(page_address) == READ_HOT);

	// This is run whether or not event is an original application IO, since we still want to do this bookkeeping even if the write event is garbage collection
	if (type == WRITE) {
		current_die_stats.live_pages += 1;

		// Keep track of live pages per LUN: Increment counter when page is written to LUN, decrement when page is invalidated in another LUN
		if (invalidated_address.valid != NONE) {
			Die_Stats& invalidated_die_stats = package_die_stats[invalidated_address.package][invalidated_address.die];
			invalidated_die_stats.live_pages -= 1;

			if (invalidated_address.package != physical_address.package || invalidated_address.die != physical_address.die) {
				/*debug*///printf("Data moved across LUNs: Written to Package %d, Die %d. Invalidated on Package %d, Die %d.\n", physical_address.package, physical_address.die, invalidated_address.package, invalidated_address.die);

				if (address_write_hot) {
					current_die_stats.unique_wh_encountered += 1;
					current_die_stats.wh_counted_already.insert(page_address);

					// If WH data is removed from a LUN, where it has been previously been counted as WH, decrease counter to undo count
					if (invalidated_die_stats.wh_counted_already.contains(page_address)) invalidated_die_stats.unique_wh_encountered -= 1;

					// Here it would have been nice to remove physical address from unique_wh_encountered bloom filter,
					// but that is not possible using the normal BF implementation.
				}
			}
		} else if (address_write_hot && !current_die_stats.wh_counted_already.contains(page_address)) {
			current_die_stats.unique_wh_encountered += 1;
			current_die_stats.wh_counted_already.insert(page_address);
		}
	}
	// print_die_stats();
}

double BloomFilter_Page_Hotness_Measurer::get_hot_data_index(event_type type, ulong page_address) const {
	double result = 0;
	double stepSize = 2 / (double) num_bloom_filters;
	const hot_bloom_filter& filter = (type == WRITE ? write_bloom : read_bloom);
	const uint& oldest_BF = (type == WRITE ? write_oldest_BF : read_oldest_BF);

	uint pos = oldest_BF;
	uint newness = 0;

	// Iterate though BFs from oldest to newest, adding recency weight to result if address in BF
	do {
		newness++;
		if (filter[pos].contains(page_address)) result += (newness * stepSize);
		pos = (pos + 1) % num_bloom_filters;
	} while (pos != oldest_BF);

	return result;
}

// Makes every address instantly hot by filling all bloom filters with 1's. The effect will fade as filters decay.
void BloomFilter_Page_Hotness_Measurer::heat_all_addresses() {
	for (uint bf = 0; bf < num_bloom_filters; bf++) {
		read_bloom[bf].insert_all_keys();
		write_bloom[bf].insert_all_keys();
	}
}

