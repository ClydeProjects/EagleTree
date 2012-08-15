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
using namespace std;

Block_manager_parent::Block_manager_parent(Ssd& ssd, FtlParent& ftl, int num_age_classes)
 : ssd(ssd),
   ftl(ftl),
   free_block_pointers(SSD_SIZE, vector<Address>(PACKAGE_SIZE)),
   free_blocks(SSD_SIZE, vector<vector<vector<Address> > >(PACKAGE_SIZE, vector<vector<Address> >(num_age_classes, vector<Address>(0)) )),
   all_blocks(0),
   greedy_gc(GREEDY_GC),
   max_age(1),
   min_age(0),
   num_age_classes(num_age_classes),
   blocks_with_min_age(),
   num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   blocks_being_garbage_collected(),
   gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(num_age_classes, set<long>())))
{
	for (uint i = 0; i < SSD_SIZE; i++) {
		Package& package = ssd.getPackages()[i];
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Die& die = package.getDies()[j];
			for (uint t = 0; t < DIE_SIZE; t++) {
				Plane& plane = die.getPlanes()[t];
				for (uint b = 0; b < PLANE_SIZE; b++) {
					Block& block = plane.getBlocks()[b];
					free_blocks[i][j][0].push_back(Address(block.get_physical_address(), PAGE));
					all_blocks.push_back(&block);
					blocks_with_min_age.insert(&block);
				}
			}
			free_block_pointers[i][j] = free_blocks[i][j][0].back();
			free_blocks[i][j][0].pop_back();
		}
	}
}

Block_manager_parent::~Block_manager_parent(void){}

void Block_manager_parent::register_erase_outcome(Event const& event, enum status status) {
	Address a = event.get_address();
	a.valid = PAGE;
	a.page = 0;

	uint age_class = sort_into_age_class(a);
	free_blocks[a.package][a.die][age_class].push_back(a);

	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	blocks_being_garbage_collected.erase(b->get_physical_address());

	if (PRINT_LEVEL > 1) {
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	num_free_pages += BLOCK_SIZE;
	num_available_pages_for_new_writes += BLOCK_SIZE;

	check_if_should_trigger_more_GC(event.get_current_time());
	Wear_Level(event);
}

void Block_manager_parent::register_write_arrival(Event const& write) {

}

uint Block_manager_parent::sort_into_age_class(Address const& a) {
	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	uint age = BLOCK_ERASES - b->get_erases_remaining();
	if (age > max_age) {
		max_age = age;
	}
	double normalized_age = (double)(age - min_age) / (max_age - min_age);
	uint klass = floor(normalized_age * num_age_classes * 0.99999);
	return klass;
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
	if (num_free_pages <= BLOCK_SIZE && how_many_gc_operations_are_scheduled() == 0) {
		perform_gc(event.get_current_time());
	}

	trim(event);
}

void Block_manager_parent::trim(Event const& event) {

	Address ra = event.get_replace_address();

	if (ra.valid == NONE) {
		return;
	}

	Block& block = ssd.getPackages()[ra.package].getDies()[ra.die].getPlanes()[ra.plane].getBlocks()[ra.block];
	Page const& page = block.getPages()[ra.page];
	uint age_class = sort_into_age_class(ra);
	long const phys_addr = block.get_physical_address();

	assert(block.get_state() == ACTIVE || block.get_state() == PARTIALLY_FREE);

	assert(page.get_state() == VALID);
	if (page.get_state() == VALID) {   // TODO: determine if I should remove the if statement
		block.invalidate_page(ra.page);
	}

	ra.valid = BLOCK;
	ra.page = 0;

	if (blocks_being_garbage_collected.count(block.get_physical_address()) == 1) {
		assert(blocks_being_garbage_collected[block.get_physical_address()] > 0);
		blocks_being_garbage_collected[block.get_physical_address()]--;
	}

	// TODO: fix thresholds for inserting blocks into GC lists. ALSO Revise the condition.
	if (blocks_being_garbage_collected.count(block.get_physical_address()) == 0 &&
			(block.get_state() == ACTIVE || block.get_state() == PARTIALLY_FREE) && block.get_pages_valid() < BLOCK_SIZE) {
		remove_as_gc_candidate(ra);
		if (PRINT_LEVEL > 1) {
			printf("Inserting as GC candidate: %ld ", phys_addr); ra.print(); printf(" with age_class %d and valid blocks: %d\n", age_class, block.get_pages_valid());
		}
		gc_candidates[ra.package][ra.die][age_class].insert(phys_addr);
		if (gc_candidates[ra.package][ra.die][age_class].size() == 1) {
			check_if_should_trigger_more_GC(event.get_current_time());
		}
	}

	if (blocks_being_garbage_collected.count(phys_addr) == 0 && block.get_state() == INACTIVE) {
		remove_as_gc_candidate(ra);
		gc_candidates[ra.package][ra.die][age_class].erase(phys_addr);
		blocks_being_garbage_collected[phys_addr] = 0;
		issue_erase(ra, event.get_current_time());
	}
	else if (blocks_being_garbage_collected.count(phys_addr) == 1 && blocks_being_garbage_collected[block.get_physical_address()] == 0) {
		assert(block.get_state() == INACTIVE);
		blocks_being_garbage_collected[phys_addr]--;
		issue_erase(ra, event.get_current_time());
	}



}

void Block_manager_parent::remove_as_gc_candidate(Address const& phys_address) {
	for (uint i = 0; i < num_age_classes; i++) {
		gc_candidates[phys_address.package][phys_address.die][i].erase(phys_address.get_linear_address());
	}
}

void Block_manager_parent::issue_erase(Address ra, double time) {
	ra.valid = BLOCK;
	ra.page = 0;

	if (PRINT_LEVEL > 1) {
		printf("block %lu", ra.get_linear_address()); ra.print(); printf(" is now invalid. An erase is issued\n");
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	Event* erase = new Event(ERASE, 0, 1, time);
	erase->set_address(ra);
	erase->set_garbage_collection_op(true);
	IOScheduler::instance()->schedule_independent_event(erase, -1, ERASE);
}

void Block_manager_parent::register_read_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == READ_COMMAND);
}

bool Block_manager_parent::can_write(Event const& write) const {
	return num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();
}

void Block_manager_parent::check_if_should_trigger_more_GC(double start_time) {
	if (num_free_pages <= BLOCK_SIZE) {
		perform_gc(start_time);
	}
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(free_block_pointers[i][j])) {
				perform_gc(i, j, 0, start_time);
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
	std::queue<Block*> blocks_to_wl;
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
		blocks_with_min_age.erase(b);
		update_blocks_with_min_age(min_age);
		min_age++;
	}

	while (!blocks_to_wl.empty() && num_available_pages_for_new_writes > blocks_to_wl.front()->get_pages_valid()) {
		Block* target = blocks_to_wl.front();
		blocks_to_wl.pop();
		num_available_pages_for_new_writes -= target->get_pages_valid();

		//migrate(target, event.get_start_time() + event.get_time_taken());
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

// This function takes a vector of channels, each of each has a vector of dies
// it finds the die with the shortest queue, and returns its ID
// if all dies are busy, the boolean field is returned as false
pair<bool, pair<uint, uint> > Block_manager_parent::get_free_die_with_shortest_IO_queue(vector<vector<Address> > const& dies) const {
	uint best_channel_id;
	uint best_die_id;
	bool can_write = false;
	bool at_least_one_address = false;
	double shortest_time = std::numeric_limits<double>::max( );
	for (uint i = 0; i < dies.size(); i++) {
		double earliest_die_finish_time = std::numeric_limits<double>::max();
		uint die_with_earliest_finish_time = 0;
		for (uint j = 0; j < dies[i].size(); j++) {
			Address pointer = dies[i][j];
			at_least_one_address = at_least_one_address ? at_least_one_address : pointer.valid == PAGE;
			bool die_has_free_pages = has_free_pages(pointer);
			uint channel_id = pointer.package;
			uint die_id = pointer.die;
			bool die_register_is_busy = ssd.getPackages()[channel_id].getDies()[die_id].register_is_busy();
			if (die_has_free_pages && !die_register_is_busy) {
				can_write = true;
				double channel_finish_time = ssd.bus.get_channel(channel_id).get_currently_executing_operation_finish_time();
				double die_finish_time = ssd.getPackages()[channel_id].getDies()[die_id].get_currently_executing_io_finish_time();
				double max = std::max(channel_finish_time,die_finish_time);

				if (die_finish_time < earliest_die_finish_time) {
					earliest_die_finish_time = die_finish_time;
					die_with_earliest_finish_time = j;
				}

				if (max < shortest_time || (max == shortest_time && die_with_earliest_finish_time == j)) {
					best_channel_id = i;
					best_die_id = j;
					shortest_time = max;
				}
			}
		}
	}
	assert(at_least_one_address);
	return pair<bool, pair<uint, uint> >(can_write, pair<uint, uint>(best_channel_id, best_die_id));
}

Address Block_manager_parent::get_free_die_with_shortest_IO_queue() const {
	pair<bool, pair<uint, uint> > best_die = get_free_die_with_shortest_IO_queue(free_block_pointers);
	if (!best_die.first) {
		return Address();
	} else {
		return free_block_pointers[best_die.second.first][best_die.second.second];
	}
}

uint Block_manager_parent::how_many_gc_operations_are_scheduled() const {
	return blocks_being_garbage_collected.size();
}

bool Block_manager_parent::has_free_pages(Address const& address) const {
	return address.valid == PAGE && address.page < BLOCK_SIZE;
}

// gives time until both the channel and die are clear
double Block_manager_parent::in_how_long_can_this_event_be_scheduled(Address const& die_address, double time_taken) const {
	uint package_id = die_address.package;
	uint die_id = die_address.die;
	double channel_finish_time = ssd.bus.get_channel(package_id).get_currently_executing_operation_finish_time();
	double die_finish_time = ssd.getPackages()[package_id].getDies()[die_id].get_currently_executing_io_finish_time();
	double max = std::max(channel_finish_time, die_finish_time);
	double time = max - time_taken;
	return time < 0 ? 0 : time;
}

// puts free blocks at the very end of the queue
struct block_valid_pages_comparator_wearwolf {
	bool operator () (const Block * i, const Block * j)
	{
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};



void Block_manager_parent::perform_gc(double start_time) {
	vector<pair<long, uint> > candidates;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < num_age_classes; k++) {
				for (set<long>::iterator iter = gc_candidates[i][j][k].begin(); iter != gc_candidates[i][j][k].end(); ++iter) {
					pair<long, uint> pair(*iter, k);
					candidates.push_back(pair);
				}
			}
		}
	}
	choose_gc_victim(candidates, start_time);
}


void Block_manager_parent::perform_gc(uint package_id, uint die_id, double start_time) {
	vector<pair<long, uint> > candidates;
	for (uint i = 0; i < gc_candidates[package_id][die_id].size(); i++) {
		for (set<long>::iterator iter = gc_candidates[package_id][die_id][i].begin(); iter != gc_candidates[package_id][die_id][i].end(); ++iter) {
			pair<long, uint> pair(*iter, i);
			candidates.push_back(pair);
		}
	}
	choose_gc_victim(candidates, start_time);
}

void Block_manager_parent::perform_gc(uint klass, double start_time) {
	vector<pair<long, uint> > candidates;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (set<long>::iterator iter = gc_candidates[i][j][klass].begin(); iter != gc_candidates[i][j][klass].end(); ++iter) {
				pair<long, uint> pair(*iter, klass);
				candidates.push_back(pair);
			}
		}
	}
	choose_gc_victim(candidates, start_time);
}

void Block_manager_parent::perform_gc(uint package_id, uint die_id, uint klass, double start_time) {
	vector<pair<long, uint> > candidates;
	for (set<long>::iterator iter = gc_candidates[package_id][die_id][klass].begin(); iter != gc_candidates[package_id][die_id][klass].end(); ++iter) {
		pair<long, uint> pair(*iter, klass);
		candidates.push_back(pair);
	}
	choose_gc_victim(candidates, start_time);
}

void Block_manager_parent::choose_gc_victim(vector<pair<long, uint> > candidates, double start_time) {
	uint min_valid_pages = BLOCK_SIZE;
	Block* best_block = NULL;
	uint best_block_age_class;
	for (uint i = 0; i < candidates.size(); i++) {
		pair<long, uint> candidate = candidates[i];
		long physical_address = candidate.first;
		Address a = Address(physical_address, BLOCK);
		Block* block = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
		uint age_class = candidate.second;
		if (block->get_pages_valid() < min_valid_pages && block->get_state() == ACTIVE) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			best_block_age_class = age_class;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}
	if (best_block != NULL && num_available_pages_for_new_writes >= best_block->get_pages_valid()) {
		Address addr = Address(best_block->get_physical_address(), BLOCK);
		assert(gc_candidates[addr.package][addr.die][best_block_age_class].count(best_block->get_physical_address()) == 1);
		gc_candidates[addr.package][addr.die][best_block_age_class].erase(best_block->get_physical_address());
		assert(gc_candidates[addr.package][addr.die][best_block_age_class].count(best_block->get_physical_address()) == 0);
		blocks_being_garbage_collected[best_block->get_physical_address()] = best_block->get_pages_valid();

		if (PRINT_LEVEL > 1) {
			printf("Triggering GC in %ld    time: %f  ", best_block->get_physical_address(), start_time); addr.print(); printf(". Migrating %d \n", best_block->get_pages_valid());
			printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
			for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
				printf("%d  ", (*iter).first);
			}
			printf("\n");
		}
		Event *gc = new Event(GARBAGE_COLLECTION, 0, BLOCK_SIZE, start_time);
		gc->set_address(addr);
		gc->set_noop(true);
		IOScheduler::instance()->schedule_independent_event(gc, 0, GARBAGE_COLLECTION);
		//migrate(best_block, start_time);
	}
}

// Reads and rewrites all valid pages of a block somewhere else
// An erase is issued in register_write_completion after the last
// page from this block has been migrated
vector<deque<Event*> > Block_manager_parent::migrate(Event* gc_event) {
	Address a = gc_event->get_address();
	Block* block = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	assert(block->get_state() != FREE);
	assert(block->get_state() != PARTIALLY_FREE);
	assert(block->get_pages_valid() <= num_available_pages_for_new_writes);

	num_available_pages_for_new_writes -= block->get_pages_valid();

	vector<deque<Event*> > migrations;
	// TODO: for DFTL, we in fact do not know the LBA when we dispatch the write. We get this from the OOB. Need to fix this.
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		if (block->getPages()[i].get_state() == VALID) {

			Address addr = Address(block->physical_address, PAGE);
			addr.page = i;
			long logical_address = ftl.get_logical_address(addr.get_linear_address());

			Event* read = new Event(READ, logical_address, 1, gc_event->get_start_time());
			read->set_address(addr);
			read->set_garbage_collection_op(true);

			Event* write = new Event(WRITE, logical_address, 1, gc_event->get_start_time());
			write->set_garbage_collection_op(true);
			write->set_replace_address(addr);

			deque<Event*> migration;
			migration.push_back(read);
			migration.push_back(write);
			migrations.push_back(migration);
		}
	}
	return migrations;
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parent::find_free_unused_block(double time) {
	for (uint i = 0; i < SSD_SIZE; i++) {
		Address address = find_free_unused_block(i, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, double time) {
	for (uint i = 0; i < PACKAGE_SIZE; i++) {
		Address address = find_free_unused_block(package_id, i, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

// finds and returns a free block from a particular die in the SSD
Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id, double time) {
	for (uint i = 0; i < free_blocks[package_id][die_id].size(); i++) {
		Address address = find_free_unused_block(package_id, die_id, i, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id, uint klass, double time) {
	assert(klass < num_age_classes);
	Address to_return;
	if (free_blocks[package_id][die_id][klass].size() > 0) {
		to_return = free_blocks[package_id][die_id][klass].back();
		free_blocks[package_id][die_id][klass].pop_back();
	} else {
		to_return = Address(0, NONE);
	}
	if (greedy_gc && free_blocks[package_id][die_id][klass].size() < 2) {
		perform_gc(package_id, die_id, klass, time);
	}
	return to_return;
}

Address Block_manager_parent::find_free_unused_block_with_class(uint klass, double time) {
	assert(klass < num_age_classes);
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Address a = free_blocks[i][j][klass].back();
			if (a.valid != NONE) {
				if (greedy_gc && free_blocks[i][j][klass].size() < 2) {
					perform_gc(i, j, klass, time);
				}
				return a;
			}
		}
	}
	return Address(0, NONE);
}

void Block_manager_parent::return_unfilled_block(Address pba) {
	if (pba.page < BLOCK_SIZE) {
		int age_class = sort_into_age_class(pba);
		free_blocks[pba.package][pba.die][age_class].push_back(pba);
	}
}

