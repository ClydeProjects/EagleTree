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

Block_manager_parent::Block_manager_parent(Ssd& ssd, FtlParent& ftl)
 : ssd(ssd),
   ftl(ftl),
   free_block_pointers(SSD_SIZE, std::vector<Address>(PACKAGE_SIZE)),
   free_blocks(SSD_SIZE, std::vector<std::vector<Address> >(PACKAGE_SIZE, std::vector<Address>(0) )),
   blocks(SSD_SIZE, std::vector<std::vector<Block*> >(PACKAGE_SIZE, std::vector<Block*>(0) )),
   all_blocks(0),
   max_age(0),
   blocks_with_min_age(),
   blocks_to_wl(),
   num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   blocks_currently_undergoing_gc()
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
					free_blocks[i][j].push_back(Address(block.get_physical_address(), PAGE));
					all_blocks.push_back(&block);
					blocks_with_min_age.insert(&block);
				}
			}
			free_block_pointers[i][j] = free_blocks[i][j].back();
			free_blocks[i][j].pop_back();
		}
	}
}

Block_manager_parent::~Block_manager_parent(void){}

void Block_manager_parent::register_erase_outcome(Event const& event, enum status status) {

	long phys_addr = event.get_address().get_linear_address();
	assert(blocks_currently_undergoing_gc.count(phys_addr) == 1);
	blocks_currently_undergoing_gc.erase(phys_addr);
	assert(blocks_currently_undergoing_gc.count(phys_addr) == 0);

	num_free_pages += BLOCK_SIZE;
	num_available_pages_for_new_writes += BLOCK_SIZE;
}

void Block_manager_parent::register_write_outcome(Event const& event, enum status status) {
	// Update stats about free pages
	assert(num_free_pages > 0);
	num_free_pages--;
	if (!event.is_garbage_collection_op()) {
		assert(num_available_pages_for_new_writes > 0);
		num_available_pages_for_new_writes--;
	}
	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE) {
		perform_emergency_garbage_collection(event.get_start_time() + event.get_time_taken());
	}

	// if the block on which a page has been invalidated is now empty, erase it
	Address ra = event.get_replace_address();
	Block& block = ssd.getPackages()[ra.package].getDies()[ra.die].getPlanes()[ra.plane].getBlocks()[ra.block];
	if (block.get_pages_invalid() == BLOCK_SIZE && blocks_currently_undergoing_gc.count(block.get_physical_address()) == 0) {
		double start_time = event.get_start_time() + event.get_time_taken();
		printf("block "); ra.print(); printf(" is now invalid. An erase is issued\n");
		migrate(&block, start_time);
	}
}

void Block_manager_parent::register_write_arrival(Event const& event) {
	assert(event.get_event_type() == WRITE);
	Address ra = event.get_replace_address();
	Block& block = ssd.getPackages()[ra.package].getDies()[ra.die].getPlanes()[ra.plane].getBlocks()[ra.block];
	Page const& page = ssd.getPackages()[ra.package].getDies()[ra.die].getPlanes()[ra.plane].getBlocks()[ra.block].getPages()[ra.page];
	if (page.get_state() == VALID) {
		block.invalidate_page(ra.page);
	}
}

void Block_manager_parent::register_read_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == READ_COMMAND);
}

bool Block_manager_parent::can_write(Event const& write) const {
	return num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();
}

void Block_manager_parent::check_if_should_trigger_more_GC(double start_time) {
	if (num_free_pages <= BLOCK_SIZE) {
		perform_emergency_garbage_collection(start_time);
	}
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (free_block_pointers[i][j].page >= BLOCK_SIZE) {
				Garbage_Collect(i, j, start_time);
			}
		}
	}
}


// TODO, at erase registration, there should be a check for WL queue. If not empty, see if can issue a WL operation. If cannot, issue an emergency GC.
// if the queue is empty, check if should trigger GC.
void Block_manager_parent::Wear_Level(Event const& event) {
	Address pba = event.get_address();
	Block* b = &ssd.getPackages()[pba.package].getDies()[pba.die].getPlanes()[pba.plane].getBlocks()[pba.block];
	uint age = BLOCK_ERASES - b->get_erases_remaining();
	uint min_age = BLOCK_ERASES - (*blocks_with_min_age.begin())->get_erases_remaining();
	if (age > max_age) {
		max_age = age;
		uint age_diff = max_age - min_age;
		if (age_diff > 500 && blocks_to_wl.size() == 0) {
			for (std::set<Block*>::const_iterator pos = blocks_with_min_age.begin(); pos != blocks_with_min_age.end(); pos++) {
				blocks_to_wl.push(*pos);
			}
			update_blocks_with_min_age(min_age + 1);
		}
	}
	else if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() > 1) {
		blocks_with_min_age.erase(b);
	}
	else if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() == 1) {
		update_blocks_with_min_age(min_age);
	}

	while (!blocks_to_wl.empty() && num_available_pages_for_new_writes > blocks_to_wl.front()->get_pages_valid()) {
		Block* target = blocks_to_wl.front();
		blocks_to_wl.pop();
		num_available_pages_for_new_writes -= target->get_pages_valid();
		migrate(target, event.get_start_time() + event.get_time_taken());
	}
}

void Block_manager_parent::update_blocks_with_min_age(uint min_age) {
	for (uint i = 0; i < all_blocks.size(); i++) {
		uint age_ith_block = BLOCK_ERASES - all_blocks[i]->get_erases_remaining();
		if (age_ith_block == min_age) {
			blocks_with_min_age.insert(all_blocks[i]);
		}
	}
}

Address Block_manager_parent::get_free_die_with_shortest_IO_queue() const {
	uint package_id;
	uint die_id;
	double shortest_time = std::numeric_limits<double>::max( );
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			bool die_has_free_pages = free_block_pointers[i][j].page < BLOCK_SIZE;
			bool die_register_is_busy = ssd.getPackages()[i].getDies()[j].register_is_busy();
			if (die_has_free_pages && !die_register_is_busy) {
				double channel_finish_time = ssd.bus.get_channel(i).get_currently_executing_operation_finish_time();
				double die_finish_time = ssd.getPackages()[i].getDies()[j].get_currently_executing_io_finish_time();
				double max = std::max(channel_finish_time,die_finish_time);
				// TODO: in case several dies within a channel have the same max, consider making a tie-breaker
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

// puts free blocks at the very end of the queue
struct block_valid_pages_comparator_wearwolf {
	bool operator () (const Block * i, const Block * j)
	{
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

// GC from the cheapest block in the device.
void Block_manager_parent::perform_emergency_garbage_collection(double start_time) {
	// first, find the cheapest block
	std::sort(all_blocks.begin(), all_blocks.end(), block_valid_pages_comparator_wearwolf());

	assert(num_free_pages <= BLOCK_SIZE);

	if (num_available_pages_for_new_writes < BLOCK_SIZE) {
		assert(blocks_currently_undergoing_gc.size() > 0);
		return;
	}
	//assert(num_available_pages_for_new_writes == BLOCK_SIZE);

	Block *target;
	bool found_suitable_block = false;
	for (uint i = 0; i < all_blocks.size(); i++) {
		target = all_blocks[i];
		if (blocks_currently_undergoing_gc.count(target->get_physical_address()) == 0 &&
				target->get_state() != FREE &&
				target->get_state() != PARTIALLY_FREE &&
				num_available_pages_for_new_writes >= target->get_pages_valid()) {
			found_suitable_block = true;
			break;
		}
	}
	assert(found_suitable_block);


	printf("Triggering emergency GC. Only %d free pages left.  ", num_free_pages);
	Address a = Address(target->get_physical_address(), BLOCK);
	a.print(); printf("\n");
	migrate(target, start_time);
}

void Block_manager_parent::Garbage_Collect(uint package_id, uint die_id, double start_time) {
	std::sort(blocks[package_id][die_id].begin(), blocks[package_id][die_id].end(), block_valid_pages_comparator_wearwolf());

	Block *target;
	bool found_suitable_block = false;
	for (uint i = 0; i < blocks[package_id][die_id].size(); i++) {
		target = blocks[package_id][die_id][i];

		if (blocks_currently_undergoing_gc.count(target->get_physical_address()) == 0 &&
				target->get_state() != FREE &&
				target->get_state() != PARTIALLY_FREE &&
				target->get_pages_valid() < BLOCK_SIZE &&
				num_available_pages_for_new_writes >= target->get_pages_valid()) {
			found_suitable_block = true;
			break;
		}
	}
	if (found_suitable_block) {
		printf("triggering GC in ");
		Address a = Address(target->get_physical_address(), BLOCK);
		a.print();
		printf("  %d invalid,  %d valid\n", target->get_pages_invalid(), target->get_pages_valid());
		migrate(target, start_time);
	}
}

void Block_manager_parent::migrate(Block const* const block, double start_time) {

	assert(block->get_state() != FREE && block->get_state() != PARTIALLY_FREE && block->get_pages_valid() <= num_available_pages_for_new_writes);
	num_available_pages_for_new_writes -= block->get_pages_valid();

	assert(blocks_currently_undergoing_gc.count(block->physical_address) == 0);
	blocks_currently_undergoing_gc.insert(block->get_physical_address());
	assert(blocks_currently_undergoing_gc.count(block->physical_address) == 1);

	Event erase = Event(ERASE, 0, 1, start_time);
	erase.set_address(Address(block->physical_address, BLOCK));
	erase.set_garbage_collection_op(true);

	std::queue<Event> events;
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		Page const& page = block->getPages()[i];
		enum page_state state = page.get_state();
		if (state == VALID) {
			Address addr = Address(block->physical_address, PAGE);
			addr.page = i;
			long logical_address = ftl.get_logical_address(addr.get_linear_address());

			Event read = Event(READ, logical_address, 1, start_time);
			read.set_address(addr);
			read.set_garbage_collection_op(true);

			Event write = Event(WRITE, logical_address, 1, start_time);
			write.set_garbage_collection_op(true);
			write.set_replace_address(addr);

			events.push(read);
			events.push(write);
		}
	}

	events.push(erase);
	IOScheduler::instance()->schedule_dependent_events(events);
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parent::find_free_unused_block() {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Address address = find_free_unused_block(i, j);
			if (address.valid != NONE) {
				return address;
			}
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id) {
	if (free_blocks[package_id][die_id].size() > 0) {
		Address a = free_blocks[package_id][die_id].back();
		free_blocks[package_id][die_id].pop_back();
		return a;
	} else {
		return Address(0, NONE);
	}
}
