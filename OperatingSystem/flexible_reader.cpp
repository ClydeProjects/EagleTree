/*
 * flexible_reader.cpp
 *
 *  Created on: Oct 25, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

// ******************* Flexible_Reader ***********************

Flexible_Reader::Flexible_Reader(FtlParent const& ftl, vector<Address_Range> ranges) :
		finished_counter(0),
		ftl(ftl),
		immediate_candidates_physical_addresses(SSD_SIZE, vector<Address>(PACKAGE_SIZE)),
		immediate_candidates_logical_addresses(SSD_SIZE, vector<long>(PACKAGE_SIZE)),
		candidate_list(SSD_SIZE, vector<vector<candidate> >(PACKAGE_SIZE, vector<candidate>())),
		pt(ranges)
{
	for (uint i = 0; i < ranges.size(); i++) {
		finished_counter += ranges[i].get_size();
	}
	assert(ranges[0].get_size() > 0);
	pt.ranges = ranges;
	pt.current_range = pt.offset_in_range = 0;
}

Flexible_Reader::progress_tracker::progress_tracker(vector<Address_Range> ranges) :
	ranges(ranges),
	current_range(0),
	offset_in_range(0),
	completion_bitmap(ranges.size(), vector<bool>(false))
{
	for (uint i = 0; i < ranges.size(); i++) {
		Address_Range ar = ranges[i];
		completion_bitmap[i] = vector<bool>(ar.get_size(), false);
	}
}

long Flexible_Reader::progress_tracker::get_next_lba() {
	long logical_address = ranges[current_range].min + offset_in_range;
	if (++offset_in_range == ranges[current_range].get_size()) {
		current_range++;
		offset_in_range = 0;
		if (++current_range < ranges.size()) {
			assert(ranges[current_range].get_size() > 0);
		}
	}
	return logical_address;
}

void Flexible_Reader::set_new_candidate() {
/*
	for (int i = 0; i < pt.completion_bitmap.size(); i++) {
		for (int j = 0; j < pt.completion_bitmap[i].size(); j++)
			cout << pt.completion_bitmap[i][j];
		cout << endl;
	}
*/
	if (!pt.finished()) {
		long logical_address = pt.get_next_lba();
		Address phys = ftl.get_physical_address(logical_address);
		int package = phys.package;
		int die = phys.die;
		if (phys.valid == PAGE && immediate_candidates_physical_addresses[package][die].valid == NONE) {
			immediate_candidates_physical_addresses[package][die] = phys;
			immediate_candidates_logical_addresses[package][die] = logical_address;
		} else if (phys.valid == PAGE) {
			candidate can = candidate(phys, logical_address);
			candidate_list[package][die].push_back(can);
		}
	}
}

Event* Flexible_Reader::read_next(double start_time) {
	assert(!is_finished());
	if (pt.current_range == 0 && pt.offset_in_range == 0) {
		for (int i = 0; i < min(20, (int)finished_counter); i++) {
			set_new_candidate();
		}
		if (finished_counter < 20) {
			pt.current_range++;
		}
	}
	return new Flexible_Read_Event(this, start_time);
}

void Flexible_Reader::register_read_commencement(Flexible_Read_Event* event) {
	Address phys_addr = event->get_address();
	int package = phys_addr.package;
	int die = phys_addr.die;
	ulong log_addr = immediate_candidates_logical_addresses[package][die];
	event->set_logical_address(log_addr);

	for (uint i = 0; i < pt.ranges.size(); i++) {
		Address_Range ar = pt.ranges[i];
		if (ar.min <= log_addr && log_addr <= ar.max) {
			int offset = log_addr - ar.min;
			assert(pt.completion_bitmap[i][offset] == false);
			pt.completion_bitmap[i][offset] = true;
			break;
		}
	}

	if (candidate_list[package][die].size() > 0) {
		candidate c = candidate_list[package][die].back();
		candidate_list[package][die].pop_back();
		immediate_candidates_physical_addresses[package][die] = c.physical_address;
		immediate_candidates_logical_addresses[package][die] = c.logical_address;
	} else {
		immediate_candidates_physical_addresses[package][die] = Address();
		immediate_candidates_logical_addresses[package][die] = UNDEFINED;
	}

	finished_counter--;
	set_new_candidate();
}

Address Flexible_Reader::get_verified_candidate_address(uint package, uint die) {
	do {
		Address a = immediate_candidates_physical_addresses[package][die];
		long int l = immediate_candidates_logical_addresses[package][die];
		if (a.valid == NONE) {
			printf("valid none very strange indeed mr. john!\n");
			// Handling a situation with an empty immediate candidate list but with candidate(s) in the candiate list
			// I'm not sure at this point whether this is merely working around a bug elsewhere.
			if (candidate_list[package][die].size() > 0) {
				a = candidate_list[package][die].back().physical_address;
				l = candidate_list[package][die].back().logical_address;
				candidate_list[package][die].pop_back();
			} else {
				return a;
			}
		}
		Address phys = ftl.get_physical_address(l);
		// If package and die matches, we're good
		if (phys.package == package && phys.die == die) {
			return a;
		// If not, move candidate to the right place matching package and die, and find a new immediate candidate
		} else {
			if (PRINT_LEVEL >= 1) {
				printf("get_verified_candidate_address(): Physical address of flexible reader candidate no longer valid. Performing correction. LBA=%ld, Phy=", l);
				a.print(); printf(" --> ");
			}
			a.package = phys.package;
			a.die     = phys.die;
			if (PRINT_LEVEL >= 1) {
				a.print();
				printf("\n");
			}
			if (immediate_candidates_physical_addresses[phys.package][phys.die].valid == NONE) {
				immediate_candidates_physical_addresses[phys.package][phys.die] = a;
				immediate_candidates_logical_addresses[phys.package][phys.die]  = l;
			} else {
				candidate_list[phys.package][phys.die].push_back(candidate(a,l));
			}
			// Put the next candidate into the immediate candidate slot
			if (candidate_list[package][die].size() > 0) {

				immediate_candidates_physical_addresses[package][die] = candidate_list[package][die].back().physical_address;
				immediate_candidates_logical_addresses[package][die]  = candidate_list[package][die].back().logical_address;
				assert(candidate_list[package][die].back().physical_address.valid != NONE);
				candidate_list[package][die].pop_back();
			// No more candidates left, so we have to return empty handed
			} else {
				immediate_candidates_physical_addresses[package][die] = Address();
				immediate_candidates_logical_addresses[package][die] = UNDEFINED;
				//printf("Out of candidates :(\n");
				return Address();
			}
		}
	} while (true /*immediate_candidates_logical_addresses[package][die] != UNDEFINED*/);
}

void Flexible_Reader::find_alternative_immediate_candidate(uint package, uint die) {
	if (candidate_list[package][die].size() == 0) {
		//printf("find_alternative_immediate_candidate: No other candidates.\n");
		return;
	}
	//printf("find_alternative_immediate_candidate: Swapping.\n");
	Address a = immediate_candidates_physical_addresses[package][die];
	long int l = immediate_candidates_logical_addresses[package][die];

	// Expensive operations here
	candidate new_candidate = *(candidate_list[package][die].begin());
	candidate_list[package][die].erase(candidate_list[package][die].begin());

	candidate_list[package][die].push_back(candidate(a,l));
	immediate_candidates_physical_addresses[package][die] = new_candidate.physical_address;
	immediate_candidates_logical_addresses[package][die]  = new_candidate.logical_address;
}
