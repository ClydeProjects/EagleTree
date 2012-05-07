/*
 * ssd_bm_parallel.cpp
 *
 *  Created on: Apr 22, 2012
 *      Author: niv
 */


/* Copyright 2011 Matias Bjørling */

/* Block Management
 *
 * This class handle allocation of block pools for the FTL
 * algorithms.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>
#include <algorithm>
#include "ssd.h"

using namespace ssd;

Block_manager_parallel::Block_manager_parallel(Ssd& ssd, FtlParent& ftl)
: blocks(SSD_SIZE, std::vector<std::vector<Block*> >(PACKAGE_SIZE, std::vector<Block*>(0) )),
  free_block_pointers(SSD_SIZE, std::vector<Address>(PACKAGE_SIZE)),
  num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
  num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
  ssd(ssd),
  ftl(ftl),
  all_blocks(0)
{
	for (uint i = 0; i < SSD_SIZE; i++) {
		Package& package = ssd.getPackages()[i];
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Die& die = package.getDies()[j];
			for (uint t = 0; t < DIE_SIZE; t++) {
				Plane& plane = die.getPlanes()[t];
				for (uint b = 0; b < PLANE_SIZE; b++) {
					Block& block = plane.getBlocks()[b];
					blocks[i][j].push_back(&block);
					all_blocks.push_back(&block);
				}
			}
			free_block_pointers[i][j] = Address(blocks[i][j][0]->get_physical_address(), PAGE);
		}
	}
}

Block_manager_parallel::~Block_manager_parallel(void)
{
	return;
}

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

	num_free_pages--;
	num_available_pages_for_new_writes--;

	if (num_free_pages <= BLOCK_SIZE) {
		Garbage_Collect(event.get_start_time() + event.get_time_taken());
	}
	else if (blockPointer.page == BLOCK_SIZE) {
		Garbage_Collect(package_id, die_id, event.get_start_time() + event.get_time_taken());
	}


}

void Block_manager_parallel::register_read_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == READ_COMMAND);
	if (status == FAILURE) {
		return;
	}
}

void Block_manager_parallel::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// if there is no free pointer for this block, set it to this one.
	if (!has_free_pages(package_id, die_id)) {
		free_block_pointers[package_id][die_id] = event.get_address();
	}

	// update num_free_pages
	num_free_pages += BLOCK_SIZE;

	// check if there are any dies on which there are no free pointers. Trigger GC on them.

	num_available_pages_for_new_writes += BLOCK_SIZE;

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
}

// Returns the address of the die with the shortest queue that has free space.
// This is to expoit parallelism for writes.
// TODO: handle case in which there is no free die
Address Block_manager_parallel::choose_write_location(Event const& event) const {
	assert(event.get_event_type() == WRITE);
	uint package_id;
	uint die_id;
	double shortest_time = std::numeric_limits<double>::max( );
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			bool die_has_free_pages = has_free_pages(i, j);
			bool die_register_is_busy = ssd.getPackages()[i].getDies()[j].register_is_busy();
			if (die_has_free_pages && !die_register_is_busy) {
				double channel_finish_time = ssd.bus.get_channel(i).get_currently_executing_operation_finish_time();
				double die_finish_time = ssd.getPackages()[i].getDies()[j].get_currently_executing_io_finish_time();
				double max = std::max(channel_finish_time,die_finish_time);
				if (max < shortest_time) {
					package_id = i;
					die_id = j;
					shortest_time = max;
				}
			}
		}
	}
	return free_block_pointers[package_id][die_id];
}

bool Block_manager_parallel::has_free_pages(uint package_id, uint die_id) const {
	return free_block_pointers[package_id][die_id].page < BLOCK_SIZE;
}

struct block_valid_pages_comparator {
	bool operator () (const Block * i, const Block * j)
	{
		if (i->get_state() == FREE){
			return true;
		}
		else if (i->get_state() == PARTIALLY_FREE){
			return false;
		}
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

/*
 * makes sure that there is at least 1 non-busy die with free space
 * and that the die is not waiting for an impending read transfer
 */
bool Block_manager_parallel::can_write(Event const& write) const {
	if (num_available_pages_for_new_writes == 0 && !write.is_garbage_collection_op()) {
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


// GC from the cheapest block in the device.
void Block_manager_parallel::Garbage_Collect(double start_time) {
	// first, find the cheapest block
	std::sort(all_blocks.begin(), all_blocks.end(), block_valid_pages_comparator());

	Block *target = all_blocks[0];
	if (target->get_state() == FREE) {
		target = all_blocks[1];
	}
	assert(target->get_state() != FREE && target->get_state() != PARTIALLY_FREE);

	assert(num_available_pages_for_new_writes >= target->get_pages_valid());
	num_available_pages_for_new_writes -= target->get_pages_valid();

	printf("Triggering emergency GC. Only %d free pages left\n", num_free_pages);

	migrate(target, start_time);
}

void Block_manager_parallel::check_if_should_trigger_more_GC(double start_time) {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(i, j)) {
				Garbage_Collect(i, j, start_time);
			}
		}
	}
}

void Block_manager_parallel::migrate(Block const* const block, double start_time) const {
	Event* erase = new Event(ERASE, 0, 1, start_time); // TODO: set start_time and copy any valid pages
	erase->set_address(Address(block->physical_address, BLOCK));
	uint dependency_code = erase->get_application_io_id();

	// must also change the mapping here. Will eventually do that.
	std::queue<Event*> events;
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		block->getPages()[0];
		Page const& page = block->getPages()[i];
		enum page_state state = page.get_state();
		if (state == VALID) {
			Event* read = new Event(READ, 0, 1, start_time);
			Address addr = Address(block->physical_address, PAGE);
			addr.page = i;
			read->set_address(addr);
			read->set_application_io_id(dependency_code);
			read->set_garbage_collection_op(true);
			long logical_address = ftl.get_logical_address(addr.get_linear_address());
			Event* write = new Event(WRITE, logical_address, 1, start_time);
			write->set_application_io_id(dependency_code);
			write->set_garbage_collection_op(true);
			events.push(read);
			events.push(write);
		}
	}

	events.push(erase);
	IOScheduler::instance()->schedule_dependent_events(events);
}

// GC from the cheapest block in a particular die
void Block_manager_parallel::Garbage_Collect(uint package_id, uint die_id, double start_time) {

	std::sort(blocks[package_id][die_id].begin(), blocks[package_id][die_id].end(), block_valid_pages_comparator());

	Block *target = blocks[package_id][die_id][0];

	if (target->get_state() == FREE) {
		free_block_pointers[package_id][die_id] = Address(target->physical_address, PAGE);
		return;
	}
	assert(target->get_state() != PARTIALLY_FREE);

	if (num_available_pages_for_new_writes < target->get_pages_valid()) {
		printf("tried to GC from die (%d %d), but not enough free pages to migrate all valid pages\n", package_id, die_id);
		return;
	}
	printf("triggering GC in die (%d %d)\n", package_id, die_id);
	num_available_pages_for_new_writes -= target->get_pages_valid();

	migrate(target, start_time);

}

