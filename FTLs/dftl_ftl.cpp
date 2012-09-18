/* Copyright 2011 Matias Bjørling */

/* dftp_ftl.cpp  */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Implementation of the DFTL described in the paper
 * "DFTL: A Flasg Translation Layer Employing Demand-based Selective Caching og Page-level Address Mappings"
 *
 * Global Mapping Table GMT
 * Global Translation Directory GTD (Maintained in memory)
 * Cached Mapping Table CMT (Uses LRU to pick victim)
 *
 * Dlpn/Dppn Data Logical/Physical Page Number
 * Mlpn/Mppn Translation Logical/Physical Page Number
 */


#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <queue>
#include <iostream>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Dftl::FtlImpl_Dftl(Controller &controller):
	FtlImpl_DftlParent(controller),
	over_provisioning_percentage(0.2)
{
	uint ssdSize = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	if (PRINT_LEVEL >= 1) {
		printf("Total size to map: %uKB\n", ssdSize * PAGE_SIZE / 1024);
		printf("Using DFTL.\n");
	}
	return;
}

FtlImpl_Dftl::~FtlImpl_Dftl(void)
{}

void FtlImpl_Dftl::read(Event *event)
{
	current_dependent_events.clear();
	MPage current = trans_map[event->get_logical_address()];
	if (current.ppn == -1) {
		fprintf(stderr, "LBA %d is unwritten, so read will be cancelled\n", event->get_logical_address(), __func__);
	}

	resolve_mapping(event, false);
	controller.stats.numFTLRead++;
	current_dependent_events.push_back(event);
	IOScheduler::instance()->schedule_events_queue(current_dependent_events);
}

void FtlImpl_Dftl::write(Event *event)
{
	current_dependent_events.clear();
	int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	assert(event->get_logical_address() < num_pages - num_mapping_pages);

	resolve_mapping(event, true);
	controller.stats.numFTLWrite++;
	current_dependent_events.push_back(event);
	IOScheduler::instance()->schedule_events_queue(current_dependent_events);
}

void FtlImpl_Dftl::trim(Event *event)
{
	IOScheduler::instance()->schedule_event(event);
}

void FtlImpl_Dftl::register_trim_completion(Event & event) {
	uint dlpn = event.get_logical_address();
	MPage current = trans_map[dlpn];
	if (current.ppn != -1)
	{
		remove_from_cache(dlpn);
		update_mapping_on_flash(dlpn, event.get_current_time());
		current.ppn = -1;
		trans_map.replace(trans_map.begin()+dlpn, current);
		num_pages_written--;
	}

	controller.stats.numFTLTrim++;

}


void FtlImpl_Dftl::register_write_completion(Event const& event, enum status result) {
	// if the write that just finished was a mapping page, we need to update the GTD to be able to find this page
	if (event.is_mapping_op()) {
		long mapping_virtual_address = get_mapping_virtual_address(event.get_logical_address());
		global_translation_directory[mapping_virtual_address] = event.get_address().get_linear_address();
		return;
	}
	MPage current = trans_map[event.get_logical_address()];

	if (event.is_original_application_io() && event.get_replace_address().valid == NONE && num_pages_written == cmt) {
		num_pages_written++;
		cmt++;
		current.cached = true;
		current.create_ts = current.modified_ts = event.get_current_time();
	}
	else if (event.is_original_application_io() && event.get_replace_address().valid == PAGE) {
		assert(current.cached);
		current.modified_ts = event.get_current_time();
	}
	// if it's a GC, it may already be in cache, in which case we update it. Else, we add it to the cache.
	else if (event.is_garbage_collection_op() && current.cached) {
		current.modified_ts = event.get_current_time();
	}

	current.ppn = event.get_address().get_linear_address();
	reverse_trans_map[current.ppn] = current.vpn;
	trans_map.replace(trans_map.begin() + event.get_logical_address(), current);
}

// TODO: fix so that current gets updated.
void FtlImpl_Dftl::register_read_completion(Event const& event, enum status result) {
	if (event.is_mapping_op()) {
		//assert(ongoing_mapping_reads.count(event.get_logical_address()) == 1);
		MPage current = trans_map[event.get_logical_address()];
		current.modified_ts = event.get_current_time();
		current.create_ts = event.get_current_time();
		current.cached = true;
		//trans_map[event.get_logical_address()] = current;
		vector<long>& entries_to_be_inserted_into_cache = ongoing_mapping_reads[event.get_logical_address()];
		cmt += entries_to_be_inserted_into_cache.size();
		while (entries_to_be_inserted_into_cache.size() > 0) {
			long lba = entries_to_be_inserted_into_cache.back();
			entries_to_be_inserted_into_cache.pop_back();
			MPage page = trans_map[lba];
			page.cached = true;
			page.create_ts = page.modified_ts = event.get_current_time();
			trans_map.replace(trans_map.begin() + lba, page);
		}
		ongoing_mapping_reads.erase(event.get_logical_address());
	}
}



// important to execute this immediately before a write is executed
// to ensure that the replace address has not been changed by GC while this write
// in the IO scheduler queue
void FtlImpl_Dftl::set_replace_address(Event& event) const {
	MPage current = trans_map[event.get_logical_address()];
	assert(event.get_event_type() == WRITE || event.get_event_type() == TRIM);
	if (current.ppn != -1) {
		Address a = Address(current.ppn, PAGE);
		assert(current.cached);
		event.set_replace_address(a);
	}
}

// important to execute this immediately before a read is executed
// to ensure that the address has not been changed by GC in the meanwhile
void FtlImpl_Dftl::set_read_address(Event& event) {
	if (event.is_mapping_op()) {
		assert(global_translation_directory.count(event.get_logical_address()) == 1);
		long physical_addr = global_translation_directory[event.get_logical_address()];
		Address a = Address(physical_addr, PAGE);
		event.set_address(a);
	} else {
		MPage current = trans_map[event.get_logical_address()];
		assert(current.cached);
		assert(current.ppn != -1);
		event.set_address(Address(current.ppn, PAGE));
	}
}
