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

double FtlImpl_DftlParent::mpage_modified_ts_compare(const FtlImpl_DftlParent::MPage& mpage)
{
	if (!mpage.cached)
		return std::numeric_limits<double>::max();

	return mpage.modified_ts;
}

FtlImpl_DftlParent::FtlImpl_DftlParent(Controller &controller):
	FtlParent(controller),
	addressSize(log(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE)/log(2)),
	addressPerPage(PAGE_SIZE / ( (ceil(addressSize / 8.0) * 2) )),
	num_mapping_pages(ceil((double)(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE) / addressPerPage)),
	global_translation_directory( )
{
	//addressPerPage = 0;
	cmt = 0;
	currentDataPage = -1;
	currentTranslationPage = -1;
	printf("Total required bits for representation: Address size: %i Total per page: %i \n", addressSize, addressPerPage);
	totalCMTentries = CACHE_DFTL_LIMIT * addressPerPage;
	printf("Number of elements in Cached Mapping Table (CMT): %i\n", totalCMTentries);

	// Initialise block mapping table.
	uint ssdSize = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	trans_map.reserve(ssdSize);
	for (uint i=0;i<ssdSize;i++)
		trans_map.push_back(MPage(i));

	reverse_trans_map = new long[ssdSize];
}

// this is called when a read or write arrives, and it is not in the cache.
// we need to get the mapping page that it is written on.
void FtlImpl_DftlParent::consult_GTD(long dlpn, Event *event)
{
	long mapping_virtual_address = get_mapping_virtual_address(event->get_logical_address());
	if (global_translation_directory.count(mapping_virtual_address) == 1) {
		long mapping_virtual_address = get_mapping_virtual_address(event->get_logical_address());
		Event* readEvent = new Event(READ, mapping_virtual_address, 1, event->get_start_time());
		readEvent->set_mapping_op(true);
		current_dependent_events.push(readEvent);
		controller.stats.numFTLRead++;
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

	event->incr_time_taken(RAM_READ_DELAY);
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
		evict_page_from_cache(event);
	} else {
		controller.stats.numCacheFaults++;
		evict_page_from_cache(event);
		uint dlpn = event->get_logical_address();
		consult_GTD(dlpn, event);
	}
}

void FtlImpl_DftlParent::evict_page_from_cache(Event *event)
{
	while (cmt >= totalCMTentries)
	{
		// Find page to evict
		MpageByModified::iterator evictit = boost::multi_index::get<1>(trans_map).begin();
		MPage evictPage = *evictit;

		assert(evictPage.cached && evictPage.create_ts >= 0 && evictPage.modified_ts >= 0);

		bool page_has_been_modified_and_should_be_updated_in_gmt = evictPage.create_ts != evictPage.modified_ts;
		if (page_has_been_modified_and_should_be_updated_in_gmt)
		{
			// we take the opportunity to also update all cached entries from the same mapping page.
			// this means that when other entires from the same mapping page are evicted, they will not trigger a write.
			int vpnBase = evictPage.vpn - evictPage.vpn % addressPerPage;
			int num_cached_entries_in_mapping_page = 1;
			for (int i=0;i<addressPerPage;i++)
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
			long virtual_mapping_page_address = get_mapping_virtual_address(evictPage.vpn);
			if (evictPage.ppn != -1 && num_cached_entries_in_mapping_page < addressPerPage) {
				Event* readEvent = new Event(READ, virtual_mapping_page_address, 1, event->get_current_time());
				readEvent->set_mapping_op(true);
				current_dependent_events.push(readEvent);
			}

			Event* write_event = new Event(WRITE, virtual_mapping_page_address, 1, event->get_current_time());
			write_event->set_mapping_op(true);
			IOScheduler::instance()->schedule_independent_event(write_event, write_event->get_logical_address(), WRITE);

			controller.stats.numFTLWrite++;
			controller.stats.numGCWrite++;
		}

		// Remove page from cache.
		cmt--;

		evictPage.cached = false;
		reset_MPage(evictPage);
		trans_map.replace(trans_map.begin()+evictPage.vpn, evictPage);
	}
}

// This method is called during a trim
void FtlImpl_DftlParent::evict_specific_page_from_cache(Event *event, long lba)
{
		// Find page to evict
		MPage evictPage = trans_map[lba];

		if (!evictPage.cached)
			return;

		assert(evictPage.cached && evictPage.create_ts >= 0 && evictPage.modified_ts >= 0);

		if (evictPage.create_ts != evictPage.modified_ts)
		{
			// Evict page
			// Inform the ssd model that it should invalidate the previous page.
			// Calculate the start address of the translation page.
			int vpnBase = evictPage.vpn - evictPage.vpn % addressPerPage;

			int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;
			int limit = addressPerPage < num_pages ? addressPerPage : num_pages;

			for (int i = 0; i < limit; i++)
			{
				MPage cur = trans_map[vpnBase+i];
				if (cur.cached)
				{
					cur.create_ts = cur.modified_ts;
					trans_map.replace(trans_map.begin()+vpnBase+i, cur);
				}
			}

			// Simulate the write to translate page
			long mapping_virtual_address = get_mapping_virtual_address(event->get_logical_address());
			Event* write_event = new Event(WRITE, mapping_virtual_address, 1, event->get_current_time());
			write_event->set_mapping_op(true);
			IOScheduler::instance()->schedule_independent_event(write_event, write_event->get_logical_address(), WRITE);

			controller.stats.numFTLWrite++;
			controller.stats.numGCWrite++;
		}
		// Remove page from cache.
		cmt--;
		evictPage.cached = false;
		reset_MPage(evictPage);
		trans_map.replace(trans_map.begin()+evictPage.vpn, evictPage);
}

long FtlImpl_DftlParent::get_logical_address(uint physical_address) const {
	return reverse_trans_map[physical_address];
}

long FtlImpl_DftlParent::get_mapping_virtual_address(long event_lba) {
	long virtual_mapping_page_number = event_lba / addressPerPage;
	return NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE - 1 - event_lba / addressPerPage;
}
