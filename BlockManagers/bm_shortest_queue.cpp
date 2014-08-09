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
#include "../ssd.h"

using namespace ssd;

Block_manager_parallel::Block_manager_parallel()
: Block_manager_parent()
{}

void Block_manager_parallel::register_write_outcome(Event const& event, enum status status) {
	Block_manager_parent::register_write_outcome(event, status);
}

void Block_manager_parallel::register_erase_outcome(Event& event, enum status status) {
	Block_manager_parent::register_erase_outcome(event, status);
	Address a = event.get_address();

	if (!has_free_pages(free_block_pointers[a.package][a.die])) {
		free_block_pointers[a.package][a.die] = find_free_unused_block(a.package, a.die, event.get_current_time());
		if (has_free_pages(free_block_pointers[a.package][a.die])) {
			Free_Space_Per_LUN_Meter::mark_new_space(a, event.get_current_time());
		}
	}
}

Address Block_manager_parallel::choose_best_address(Event& write) {
	return get_free_block_pointer_with_shortest_IO_queue();
}

Address Block_manager_parallel::choose_any_address(Event const& write) {
	return get_free_block_pointer_with_shortest_IO_queue();
}
