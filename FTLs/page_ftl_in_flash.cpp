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

void ftl_cache::register_write_arrival(Event *event)
{
	int la = event->get_logical_address();
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table.at(la);
		e.hotness++;
		e.fixed++;
	}
	else if (!event->is_mapping_op()) {
		entry e;
		e.fixed = 1;
		e.hotness = 1;
		cached_mapping_table[la] = e;
	}
	else {
		assert(false);
	}
}

void ftl_cache::register_write_completion(Event const& event) {
	if (event.is_garbage_collection_op() && !event.is_original_application_io() && !event.is_mapping_op()) {
		if (cached_mapping_table.count(event.get_logical_address()) == 0) {
			entry e;
			e.timestamp = event.get_current_time();
			e.dirty = true;
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
	//try_clear_space_in_mapping_cache(event.get_current_time());
}

void ftl_cache::handle_mapping_dependency(Event *e) {
	long fixed = 0;
	if (cached_mapping_table.count(e->get_logical_address()) == 0) {
		entry entry;
		entry.hotness++;
		fixed = 0;
		cached_mapping_table[e->get_logical_address()] = entry;
		if (entry.dirty) {
			eviction_queue_dirty.push(e->get_logical_address());
		} else  {
			eviction_queue_clean.push(e->get_logical_address());
		}
	}
	else {
		entry& entry = cached_mapping_table.at(e->get_logical_address());
		entry.hotness++;
		fixed = entry.fixed;
	}
	if (e->get_event_type() == WRITE && fixed == 0) {
		entry& entry = cached_mapping_table.at(e->get_logical_address());
		entry.fixed++;
	}
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
