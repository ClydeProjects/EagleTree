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
#include "../ssd.h"

using namespace ssd;

Block_manager_roundrobin::Block_manager_roundrobin(bool channel_alternation)
:	Block_manager_parent(),
	channel_alternation(channel_alternation)
{}

Block_manager_roundrobin::~Block_manager_roundrobin(void)
{}

void Block_manager_roundrobin::register_write_outcome(Event const& event, enum status status) {
	// assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_write_outcome(event, status);
	move_address_cursor();
}

void Block_manager_roundrobin::register_erase_outcome(Event& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	Address a = event.get_address();

	// if there is no free pointer for this block, set it to this one.
	if (!has_free_pages(free_block_pointers[a.package][a.die])) {
		free_block_pointers[a.package][a.die] = find_free_unused_block(a.package, a.die);
	}

	check_if_should_trigger_more_GC(event);
}

Address Block_manager_roundrobin::choose_best_address(Event& write) { // const
	return free_block_pointers[address_cursor.package][address_cursor.die];
}

Address Block_manager_roundrobin::choose_any_address(Event const& write) {
	return get_free_block_pointer_with_shortest_IO_queue();
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
