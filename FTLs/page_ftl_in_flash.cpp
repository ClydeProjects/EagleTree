/*
 * page_ftl_in_flash.cpp
 *
 *  Created on: Jan 10, 2015
 *      Author: niv
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <ctgmath>
#include "../ssd.h"

using namespace ssd;

int ftl_cache::CACHED_ENTRIES_THRESHOLD = 10000;

void ftl_cache::register_write_arrival(Event const& event)
{
	int la = event.get_logical_address();
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table.at(la);
		e.hotness++;
		e.fixed++;
	}
	else if (!event.is_mapping_op()) {
		entry e;
		e.fixed = 1;
		e.hotness = 1;
		e.synch_flag = false;
		cached_mapping_table[la] = e;
	}
	else {
		assert(false);
	}
}

void ftl_cache::handle_read_dependency(Event* e) {
	if (cached_mapping_table.count(e->get_logical_address()) == 0) {
		ftl_cache::entry entry;
		entry.hotness++;
		entry.synch_flag = true;
		cached_mapping_table[e->get_logical_address()] = entry;
		eviction_queue_clean.push(e->get_logical_address());
	}
	else {
		ftl_cache::entry& entry = cached_mapping_table.at(e->get_logical_address());
		entry.hotness++;
	}
}

bool ftl_cache::register_read_arrival(Event* app_read) {
	int la = app_read->get_logical_address();
	if (cached_mapping_table.count(la) == 1) {
		ftl_cache::entry& e = cached_mapping_table.at(la);
		e.hotness++;
		return true;
	}
	return false;
}

void ftl_cache::register_write_completion(Event const& event) {
	assert(!event.is_mapping_op());
	if (event.is_garbage_collection_op() && !event.is_original_application_io()) {
		if (cached_mapping_table.count(event.get_logical_address()) == 0) {
			entry e;
			e.timestamp = event.get_current_time();
			e.dirty = true;
			e.synch_flag = true;
			cached_mapping_table[event.get_logical_address()] = e;
			eviction_queue_dirty.push(event.get_logical_address());
		}
		else {
			entry& e = cached_mapping_table.at(event.get_logical_address());
			e.dirty = true;
			e.timestamp = event.get_current_time();
			e.fixed = 0;
		}
	}
	else if (event.is_original_application_io()) {
		entry& e = cached_mapping_table.at(event.get_logical_address());
		e.fixed = 0;
		e.dirty = true;
		eviction_queue_dirty.push(event.get_logical_address());
		e.timestamp = event.get_current_time();
	}
	else {
		assert(false);  // just since I'm not immediately sure what should happen here
	}
	//try_clear_space_in_mapping_cache(event.get_current_time());
}

void ftl_cache::iterate(long& victim_key, entry& victim_entry, bool allow_choosing_dirty) {
	queue<long>& queue = allow_choosing_dirty ? eviction_queue_dirty : eviction_queue_clean;
	for (int i = 0; i < queue.size(); i++) {
		long addr = queue.front();
		queue.pop();
		if (cached_mapping_table.count(addr) == 0) {
			continue;
		}
		entry& e = cached_mapping_table.at(addr);
		if (!allow_choosing_dirty && e.dirty) {
			eviction_queue_dirty.push(addr);
		}
		else if (allow_choosing_dirty && !e.dirty) {
			eviction_queue_clean.push(addr);
		}
		else if (!e.fixed && e.hotness == 0) {
			victim_key = addr;
			victim_entry = e;
			return;
		}
		else if (e.dirty == allow_choosing_dirty) {
			e.hotness = e.hotness == 0 ? 0 : e.hotness - 1;
			queue.push(addr);
		}
	}
}

void ftl_cache::clear_clean_entries(double time) {
	while (cached_mapping_table.size() >= CACHED_ENTRIES_THRESHOLD && erase_victim(time, false) != UNDEFINED);
}

int ftl_cache::choose_dirty_victim(double time) {
	return erase_victim(time, true);
}

bool ftl_cache::mark_clean(int key, double time) {
	if (cached_mapping_table.count(key) == 0) {
		return false;
	}
	ftl_cache::entry& e = cached_mapping_table.at(key);
	bool was_dirty = e.dirty;
	assert(e.fixed >= 0);
	if (e.timestamp <= time && e.hotness == 0 && e.fixed == 0) {
		cached_mapping_table.erase(key);
	}
	else if (e.timestamp <= time && e.dirty) {
		e.dirty = false;
	}
	return was_dirty;
}

bool ftl_cache::contains(int key) const {
	return cached_mapping_table.count(key) == 1;
}

void ftl_cache::set_synchronized(int key) {
	if (cached_mapping_table.count(key) == 1) {
		ftl_cache::entry& e = cached_mapping_table.at(key);
		e.synch_flag = true;
	}
}

void flash_resident_page_ftl::update_bitmap(vector<bool>& bitmap, Address block_addr) {
	int block_id = block_addr.get_block_id();
	Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
	for (int i = 0; i < BLOCK_SIZE; i++) {
		int log_addr = page_mapping->get_logical_address(block_id * BLOCK_SIZE + i);
		int orig_logical_addr = block->get_page(i).get_logical_addr();
		if (log_addr == UNDEFINED && bitmap[i] == true) {
			bitmap[i] = false;


			cache->set_synchronized(orig_logical_addr);
			//printf("orig log addr: %d \n", orig_logical_addr);
		}
		if (log_addr != UNDEFINED) {
			bool bit = bitmap[i];
			if (bit != true) {
				printf("warning: address %d is still valid, yet the bit for it is false.   block id: %d  page offset: %d\n", log_addr, block_id, i);
			}
			assert(bit == true);
		}

	}

}

void flash_resident_page_ftl::set_synchronized(int logical_address) {
	assert(cache->contains(logical_address));
	cache->set_synchronized(logical_address);
}

// Uses a clock entry replacement policy
int ftl_cache::erase_victim(double time, bool allow_flushing_dirty) {
	//printf("flush called. num dirty: %d     cache size: %d\n", get_num_dirty_entries(), cached_mapping_table.size());
	// start at a given location
	// find first entry with hotness 0 and make that the target, or make full traversal and identify least hot entry
	long victim = UNDEFINED;
	entry victim_entry;
	victim_entry.hotness = SHRT_MAX;

	iterate(victim, victim_entry, allow_flushing_dirty);

	if (victim == -1) {
		//printf("Warning, could not find a victim to flush from cache\n");
		return UNDEFINED;
	}

	// if entry is clean, just erase it. Otherwise, need some mapping IOs.
	if (!victim_entry.dirty) {
		//printf("erase %d\n", victim);
		cached_mapping_table.erase(victim);
		return victim;
	}
	return victim;
}

int ftl_cache::get_num_dirty_entries() const {
	int num_dirty = 0;
	for (auto i : cached_mapping_table) {
		if (i.second.dirty) {
			num_dirty++;
		}
	}
	return num_dirty;
}
