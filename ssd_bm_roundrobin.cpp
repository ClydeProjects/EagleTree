/*
 * ssd_bm_roundrobin.cpp
 *
 *  Created on: Jul 19, 2012
 *      Author: mks (based directly on ssd_bm_parallel)
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>
#include <algorithm>
#include "ssd.h"

using namespace ssd;

Block_manager_roundrobin::Block_manager_roundrobin(Ssd& ssd, FtlParent& ftl)
: Block_manager_parent(ssd, ftl)
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
			perform_gc(package_id, die_id, event.get_start_time() + event.get_time_taken());
		}
	}
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
	Wear_Level(event);
}

bool Block_manager_roundrobin::has_free_pages(uint package_id, uint die_id) const {
	return free_block_pointers[package_id][die_id].page < BLOCK_SIZE;
}

// NOTES:
// ------
// Cycle through packages (=channels)
// either: channel1die1, channel1die2, channel2die1, channel2die2
//     or: channel1die1, channel2die1, channel1die2, channel2die2
// SSD - PACKAGE - DIE - PLANE - BLOCK - PAGE
// Ignore planes: Assume one one
// Blocks/pages are already taken care up, blocks are filled up with pages sequentially
// Package(channel) and die is the shit around here.

pair<double, Address> Block_manager_roundrobin::write(Event const& write) { // const
	pair<double, Address> result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		result.first = 1;
		return result;
	}

	//result.second = get_free_die_with_shortest_IO_queue();
	result.second = address_cursor;
	move_address_cursor();

	if (result.second.valid == NONE) {
		result.first = 1;
		return result;
	}

	result.first = in_how_long_can_this_event_be_scheduled(result.second, write.get_start_time() + write.get_time_taken());

	printf("package, die, plane, block, page, (int) valid:\n"); result.second.print(stdout);
	/*debug*/printf("Block manager parallel decided that write should go to package %d, die %d, in %d time.\n", result.second.package, result.second.die, result.first);

	return result;
}

// Moves the address cursor to next position in a round-robin fashion
void Block_manager_roundrobin::move_address_cursor() {
	address_cursor.page = (address_cursor.page + 1) % BLOCK_SIZE;
	if (address_cursor.page == 0) {
		address_cursor.block = (address_cursor.block + 1) % PLANE_SIZE;
		if (address_cursor.block == 0) {
			address_cursor.plane = (address_cursor.plane + 1) % DIE_SIZE;
			if (address_cursor.plane == 0) {
				address_cursor.die = (address_cursor.die + 1) % PACKAGE_SIZE;
				if (address_cursor.die == 0) {
					address_cursor.package = (address_cursor.package + 1) % SSD_SIZE;
				}
			}
		}
	}
}
