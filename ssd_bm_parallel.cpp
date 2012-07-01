/*
 * ssd_bm_parallel.cpp
 *
 *  Created on: Apr 22, 2012
 *      Author: niv
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>
#include <algorithm>
#include "ssd.h"

using namespace ssd;

Block_manager_parallel::Block_manager_parallel(Ssd& ssd, FtlParent& ftl)
: Block_manager_parent(ssd, ftl)
{}

Block_manager_parallel::~Block_manager_parallel(void)
{}

void Block_manager_parallel::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}

	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	Address blockPointer = free_block_pointers[package_id][die_id];
	blockPointer.page = blockPointer.page + 1;
	free_block_pointers[package_id][die_id] = blockPointer;

	if (blockPointer.page == BLOCK_SIZE) {
		Garbage_Collect(package_id, die_id, event.get_start_time() + event.get_time_taken());
	}
}

void Block_manager_parallel::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// if there is no free pointer for this block, set it to this one.
	if (!has_free_pages(package_id, die_id)) {
		free_block_pointers[package_id][die_id] = event.get_address();
	} else {
		free_blocks[package_id][die_id].push_back(event.get_address());
	}

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
	Wear_Level(event);
}

// Returns the address of the die with the shortest queue that has free space.
// This is to expoit parallelism for writes.
// TODO: handle case in which there is no free die
Address Block_manager_parallel::choose_write_location(Event const& event) const {
	assert(event.get_event_type() == WRITE);
	return get_free_die_with_shortest_IO_queue();
}

bool Block_manager_parallel::has_free_pages(uint package_id, uint die_id) const {
	return free_block_pointers[package_id][die_id].page < BLOCK_SIZE;
}

/*
 * makes sure that there is at least 1 non-busy die with free space
 * and that the die is not waiting for an impending read transfer
 */
bool Block_manager_parallel::can_write(Event const& write) const {
	if (!Block_manager_parent::can_write(write)) {
		return false;
	}

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			bool has_space = has_free_pages(i, j);
			bool non_busy = !ssd.getPackages()[i].getDies()[j].register_is_busy();
			if (has_space && non_busy) {
				return true;
			}
		}
	}
	return false;
}


