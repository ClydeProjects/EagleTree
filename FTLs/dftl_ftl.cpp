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
	uint ssdSize = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;
	printf("Total size to map: %uKB\n", ssdSize * PAGE_SIZE / 1024);
	printf("Using DFTL.\n");
	return;
}

FtlImpl_Dftl::~FtlImpl_Dftl(void)
{}

void FtlImpl_Dftl::read(Event *event)
{
	MPage current = trans_map[event->get_logical_address()];
	if (current.ppn == -1) {
		fprintf(stderr, "LBA %d is unwritten, so read will be cancelled\n", event->get_logical_address(), __func__);
	}

	resolve_mapping(event, false);
	controller.stats.numFTLRead++;
	current_dependent_events.push(event);
	IOScheduler::instance()->schedule_dependent_events(current_dependent_events);
}

void FtlImpl_Dftl::write(Event *event)
{
	resolve_mapping(event, true);
	controller.stats.numFTLWrite++;
	current_dependent_events.push(event);
	IOScheduler::instance()->schedule_dependent_events(current_dependent_events);
}

void FtlImpl_Dftl::trim(Event *event)
{
	//assert(event.get_logical_address() < NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE * (1 - over_provisioning_percentage));
	uint dlpn = event->get_logical_address();

	event->set_address(Address(0, PAGE));

	MPage current = trans_map[dlpn];

	if (current.ppn != -1)
	{
		Address address = Address(current.ppn, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);
		evict_specific_page_from_cache(event, dlpn);
		current.ppn = -1;
		trans_map.replace(trans_map.begin()+dlpn, current);
	}

	controller.stats.numFTLTrim++;

	//return controller.issue(event);
}


void FtlImpl_Dftl::register_write_completion(Event const& event, enum status result) {
	if (result == FAILURE) {
		return;
	}
	uint logical = event.get_logical_address();
	uint physical = event.get_address().get_linear_address();
	MPage current = trans_map[logical];

	// if it's a normal write, we assume its mapping is cached, and we update the modified ts to indicate change
	if (event.is_original_application_io()) {
		if (current.ppn == -1) {
			current.modified_ts = event.get_current_time();
			current.create_ts = event.get_current_time();
			current.cached = true;
			cmt++;
		} else {
			assert(current.cached);
			current.modified_ts = event.get_current_time();
		}
	}

	// if it's a GC, it may already be in cache, in which case we update it. Else, we add it to the cache.
	if (event.is_garbage_collection_op()) {
		if (current.cached) {
			current.modified_ts = event.get_current_time();
		}
		else {
			current.modified_ts = event.get_current_time();
			current.create_ts = event.get_current_time();
			current.cached = true;
			cmt++;
		}
	}

	current.ppn = physical;
	reverse_trans_map[physical] = current.vpn;
	trans_map.replace(trans_map.begin() + logical, current);

	// if the write that just finished was a mapping page, we need to update the GTD to be able to find this page
	if (event.is_mapping_op()) {
		long mvpn = logical / 512;
		long original = global_translation_directory[mvpn];

		Address address = Address(original, PAGE);
		Block *block = controller.get_block_pointer(address);
		block->invalidate_page(address.page);

		global_translation_directory[mvpn] = physical;
	}
}

void FtlImpl_Dftl::register_read_completion(Event const& event, enum status result) {
	assert(event.get_event_type() == READ_TRANSFER);
	if (result != SUCCESS) {
		return;
	}
	if (event.is_mapping_op()) {
		MPage current = trans_map[event.get_logical_address()];
		current.modified_ts = event.get_start_time() + event.get_time_taken();
		current.create_ts = event.get_start_time() + event.get_time_taken();
		current.cached = true;
		trans_map.replace(trans_map.begin()+event.get_logical_address(), current);
		cmt++;
	}
}

// important to execute this immediately before a write is executed
// to ensure that the replace address has not been changed by GC while this write
// in the IO scheduler queue
void FtlImpl_Dftl::set_replace_address(Event& event) const {
	MPage current = trans_map[event.get_logical_address()];
	assert(event.get_event_type() == WRITE);
	Address a = Address(current.ppn, PAGE);
	if (current.ppn != -1) {
		assert(current.cached);
		event.set_replace_address(a);
	}
}

// important to execute this immediately before a read is executed
// to ensure that the address has not been changed by GC in the meanwhile
void FtlImpl_Dftl::set_read_address(Event& event) const {
	assert(event.get_event_type() == READ_COMMAND || event.get_event_type() == READ_TRANSFER);
	MPage current = trans_map[event.get_logical_address()];
	assert(current.cached);
	assert(current.ppn != -1);
	event.set_address(Address(current.ppn, PAGE));
}
