/*
 * ssd_bm_roundrobin.cpp
 *
 *  Created on: Jul 19, 2012
 *      Author: mks (based directly on ssd_bm_parallel, which, should be noted, contains duplicate code)
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>
#include <algorithm>
#include "ssd.h"

using namespace ssd;

Block_manager_roundrobin::Block_manager_roundrobin(Ssd& ssd, FtlParent& ftl, bool channel_alternation)
:	Block_manager_parent(ssd, ftl),
	channel_alternation(channel_alternation)
{}

Block_manager_roundrobin::~Block_manager_roundrobin(void)
{}

void Block_manager_roundrobin::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_write_outcome(event, status);

	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	Address blockPointer = free_block_pointers[package_id][die_id];
	blockPointer.page = blockPointer.page + 1;
	free_block_pointers[package_id][die_id] = blockPointer;

	if (free_block_pointers[package_id][die_id].page == BLOCK_SIZE) {
		Address free_block = find_free_unused_block(package_id, die_id, event.get_current_time());
		if (free_block.valid == PAGE) {
			free_block_pointers[package_id][die_id] = free_block;
		} else {
			//perform_gc(package_id, die_id, event.get_start_time() + event.get_time_taken());
			schedule_gc(event.get_current_time(), package_id, die_id);
		}
	}

	move_address_cursor();
}

void Block_manager_roundrobin::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	Address a = event.get_address();

	// if there is no free pointer for this block, set it to this one.
	if (!has_free_pages(a.package, a.die)) {
		free_block_pointers[a.package][a.die] = find_free_unused_block(a.package, a.die);
	}

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
}

bool Block_manager_roundrobin::has_free_pages(uint package_id, uint die_id) const {
	return free_block_pointers[package_id][die_id].page < BLOCK_SIZE;
}

pair<double, Address> Block_manager_roundrobin::write(Event const& write) { // const
	pair<double, Address> result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		result.first = 1;
		return result;
	}

	result.second = free_block_pointers[address_cursor.package][address_cursor.die];

	if (result.second.valid == NONE) {
		result.first = 1;
		return result;
	}

	result.first = in_how_long_can_this_event_be_scheduled(result.second, write.get_start_time() + write.get_time_taken());

	return result;
}

// Moves the address cursor to next position in a round-robin fashion
void Block_manager_roundrobin::move_address_cursor() {
	if (channel_alternation) {
		address_cursor.package = (address_cursor.package + 1) % SSD_SIZE;
		if (address_cursor.package == 0) address_cursor.die = (address_cursor.die + 1) % PACKAGE_SIZE;
	} else {
		address_cursor.die = (address_cursor.die + 1) % PACKAGE_SIZE;
		if (address_cursor.die == 0) address_cursor.package = (address_cursor.package + 1) % SSD_SIZE;
	}
}
