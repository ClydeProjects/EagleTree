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
#include <limits>
#include "../ssd.h"

using namespace ssd;

FtlImpl_DftlParent::MPage::MPage(long vpn)
{
	this->vpn = vpn;
	this->ppn = -1;
	this->create_ts = -1;
	this->modified_ts = -1;
	this->cached = false;
}

bool FtlImpl_DftlParent::MPage::has_been_modified() {
	return create_ts != modified_ts;
}

double FtlImpl_DftlParent::mpage_modified_ts_compare(const FtlImpl_DftlParent::MPage& mpage)
{
	if (!mpage.cached)
		return std::numeric_limits<double>::max();

	return mpage.modified_ts;
}

FtlImpl_DftlParent::FtlImpl_DftlParent(Controller &controller):
	FtlParent(controller),
	addressSize(log(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE)/log(2)),
	addressPerPage(PAGE_SIZE / ( (ceil(addressSize / 8.0) * 2) )),
	num_mapping_pages(ceil((double)(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE) / addressPerPage)),
	global_translation_directory( ),
	totalCMTentries(CACHE_DFTL_LIMIT * addressPerPage),
	num_pages_written(0)
{
	//addressPerPage = 0;
	cmt = 0;
	if (PRINT_LEVEL > 0) {
		printf("Total required bits for representation: Address size: %i Total per page: %i \n", addressSize, addressPerPage);
		printf("Number of elements in Cached Mapping Table (CMT): %i\n", totalCMTentries);
	}
	// Initialise block mapping table.
	uint ssdSize = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;

	trans_map.reserve(ssdSize);
	for (uint i=0;i<ssdSize;i++)
		trans_map.push_back(MPage(i));

	reverse_trans_map = new long[ssdSize];
}

// this is called when a read or write arrives, and it is not in the cache.
// we need to get the mapping page that it is written on.
void FtlImpl_DftlParent::consult_GTD(long dlpn, Event *event)
{
	long mapping_address = get_mapping_virtual_address(event->get_logical_address());
	if (num_pages_written > cmt && global_translation_directory.count(mapping_address) == 1 && ongoing_mapping_reads.count(mapping_address) == 0) {
		Event* readEvent = new Event(READ, mapping_address, 1, event->get_start_time());
		readEvent->set_mapping_op(true);
		//readEvent->set_application_io_id(event->get_application_io_id());
		if (readEvent->get_id() == 244 || readEvent->get_id() == 246) {
			int i = 0;
			i++;
		}
		//current_dependent_events.push_front(readEvent);
		controller.stats.numFTLRead++;
		ongoing_mapping_reads[mapping_address].push_back(event->get_logical_address());
	}
	else if (global_translation_directory.count(mapping_address) == 1 && ongoing_mapping_reads.count(mapping_address) == 1) {
		ongoing_mapping_reads[mapping_address].push_back(dlpn);
	}
}

void FtlImpl_DftlParent::reset_MPage(FtlImpl_DftlParent::MPage &mpage)
{
	mpage.create_ts = -2;
	mpage.modified_ts = -2;
}

bool FtlImpl_DftlParent::lookup_CMT(long dlpn, Event *event)
{
	if (!trans_map[dlpn].cached)
		return false;

	event->incr_execution_time(RAM_READ_DELAY);
	controller.stats.numMemoryRead++;

	return true;
}


FtlImpl_DftlParent::~FtlImpl_DftlParent(void)
{
	delete[] reverse_trans_map;
}

/* 1. Lookup in CMT if the mapping exist
 * 2. If, then serve
 * 3. If not, then goto GDT, lookup page
 * 4. If CMT full, evict a page
 */
void FtlImpl_DftlParent::resolve_mapping(Event *event, bool isWrite)
{
	if (lookup_CMT(event->get_logical_address(), event)) {
		controller.stats.numCacheHits++;

	} else {
		controller.stats.numCacheFaults++;
		uint dlpn = event->get_logical_address();
		consult_GTD(dlpn, event);
	}
	evict_page_from_cache(event->get_current_time());
}

// while there are too many pages in the cache, find a page that has not been accessed for
void FtlImpl_DftlParent::evict_page_from_cache(double time)
{
	while (cmt >= totalCMTentries)
	{
		MpageByModified::iterator evictit = boost::multi_index::get<1>(trans_map).begin();
		MPage evictPage = *evictit;
		assert(evictPage.cached && evictPage.create_ts >= 0 && evictPage.modified_ts >= 0);
		if (evictPage.has_been_modified())
		{
			update_mapping_on_flash(evictPage.vpn, time);
		}
		remove_from_cache(evictPage.vpn);
	}
}

void FtlImpl_DftlParent::remove_from_cache(long lba) {
	MPage victim = trans_map[lba];
	cmt--;
	victim.cached = false;
	reset_MPage(victim);
	trans_map.replace(trans_map.begin() + lba, victim);
}

void FtlImpl_DftlParent::update_mapping_on_flash(long lba, double time) {
	int vpnBase = lba - lba % addressPerPage;
	int num_cached_entries_in_mapping_page = 1;
	int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
 	int limit = addressPerPage < num_pages ? addressPerPage : num_pages;
	for (int i = 0; i < limit; i++)
	{
		MPage cur = trans_map[vpnBase+i];
		if (cur.cached)
		{
			cur.create_ts = cur.modified_ts;
			trans_map.replace(trans_map.begin()+vpnBase+i, cur);
			num_cached_entries_in_mapping_page++;
		}
	}

	// if not all entries from the mapping page are cached, need to read the page
	long virtual_mapping_page_address = get_mapping_virtual_address(lba);
	deque<Event*> mapping_events;

	Event* write_event = new Event(WRITE, virtual_mapping_page_address, 1, time);
	write_event->set_mapping_op(true);

	if (global_translation_directory.count(virtual_mapping_page_address) == 1 && num_cached_entries_in_mapping_page < addressPerPage) {
		Event* readEvent = new Event(READ, virtual_mapping_page_address, 1, time);
		readEvent->set_mapping_op(true);
		readEvent->set_application_io_id(write_event->get_application_io_id());
		mapping_events.push_back(readEvent);
	}

	mapping_events.push_back(write_event);
	//IOScheduler::instance()->schedule_events_queue(mapping_events);
	controller.stats.numFTLWrite++;
	controller.stats.numGCWrite++;
}

long FtlImpl_DftlParent::get_logical_address(uint physical_address) const {
	return reverse_trans_map[physical_address];
}

long FtlImpl_DftlParent::get_mapping_virtual_address(long event_lba) {
	long virtual_mapping_page_number = event_lba / addressPerPage;
	return NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE - 1 - event_lba / addressPerPage;
}
