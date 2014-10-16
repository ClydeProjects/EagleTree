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

bm_gc_locality::bm_gc_locality()
: pointers_for_ongoing_gc_operations(),
  partially_used_blocks(SSD_SIZE, vector<queue<Address> >(PACKAGE_SIZE, queue<Address>())),
  Block_manager_parent()
{
	//partially_used_blocks[0][0].push(Address());
}

void bm_gc_locality::register_write_outcome(Event const& event, enum status status) {
	Address a = event.get_address();

	if (event.get_id() == 1061262) {
		int i = 0;
		i++;
	}

	if (partially_used_blocks[a.package][a.die].size() > 0 &&
			a.compare(free_block_pointers[a.package][a.die]) >= BLOCK &&
			free_block_pointers[a.package][a.die].valid == PAGE &&
			free_block_pointers[a.package][a.die].page + 1 == BLOCK_SIZE) {
		Address partially_free_pointer = partially_used_blocks[a.package][a.die].front();
		free_block_pointers[a.package][a.die] = partially_free_pointer;
		assert(partially_free_pointer.package == a.package && partially_free_pointer.die == a.die);
		partially_used_blocks[a.package][a.die].pop();
	}

	if (event.is_garbage_collection_op()) {
		Address block_addr = event.get_replace_address();
		Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
		assert(pointers_for_ongoing_gc_operations.count(block) == 1);
		pointers_for_ongoing_gc_operations[block].page++;
	}

	if (event.get_id() == 1046532) {
		a.print();
		free_block_pointers[a.package][a.die].print();
		printf("%d\n", get_num_free_blocks(a.package, a.die));
	}

	// The reason for this is so in the method below, we don't claim the last block in the LUN
	if (free_block_pointers[a.package][a.die].page + 1 == BLOCK_SIZE &&
			a.compare(free_block_pointers[a.package][a.die]) >= BLOCK &&
			get_num_free_blocks(a.package, a.die) <= 1) {
		free_block_pointers[a.package][a.die] = Address();
	}

	int temp = GREED_SCALE;
	if (get_num_free_blocks(a.package, a.die) + partially_used_blocks[a.package][a.die].size() >= GREED_SCALE) {
		GREED_SCALE = 0;
	}
	Block_manager_parent::register_write_outcome(event, status);
	GREED_SCALE = temp;

	//static int sum = 0;

	//if (get_num_pointers_with_free_space() < SSD_SIZE * PACKAGE_SIZE) {
		//printf("get_num_pointers_with_free_space:   %d\n", get_num_pointers_with_free_space());
	//}
	//printf("gc scheduled: %d     num pointers: %d\n", migrator->how_many_gc_operations_are_scheduled(), this->get_num_pointers_with_free_space());
}

Address bm_gc_locality::get_block_for_gc(int package, int die, double current_time) {
	vector<int> packages = Random_Order_Iterator::get_iterator(SSD_SIZE);
	for (auto p : packages) {
		vector<int> dies = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
		for (auto d : dies) {
			if (package == p && die == d) {
				continue;
			}
			Address a = find_free_unused_block(p, d, current_time);
			if (a.valid != NONE) {
				return a;
			}
		}
	}
	return find_free_unused_block(package, die, current_time);
}

bool bm_gc_locality::may_garbage_collect_this_block(Block* block, double current_time) {
	int temp = GREED_SCALE;
	GREED_SCALE = 0;
	Address victim_addr = Address(block->get_physical_address(), BLOCK);

	//Address a = get_block_for_gc(victim_addr.package, victim_addr.die, current_time);
	//Address a = find_free_unused_block(victim_addr.package, victim_addr.die, current_time);
	Address a = find_free_unused_block(current_time);
	GREED_SCALE = temp;
	if (a.valid == NONE)
	{
		assert(migrator->how_many_gc_operations_are_scheduled() > 0);
		return false;
	}

	pointers_for_ongoing_gc_operations[block] = a;
	return true;
}

void bm_gc_locality::register_erase_outcome(Event& event, enum status status) {

	Address block_addr = event.get_address();
	Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
	assert(pointers_for_ongoing_gc_operations.count(block) == 1);
	Address partially_free_block = pointers_for_ongoing_gc_operations.at(block);
	if (has_free_pages(partially_free_block) && !has_free_pages(free_block_pointers[partially_free_block.package][partially_free_block.die])) {
		free_block_pointers[partially_free_block.package][partially_free_block.die] = partially_free_block;
	}
	else if (has_free_pages(partially_free_block)) {
		partially_used_blocks[partially_free_block.package][partially_free_block.die].push(partially_free_block);
	}
	pointers_for_ongoing_gc_operations.erase(block);

	Block_manager_parent::register_erase_outcome(event, status);
	Address a = event.get_address();

	if (!has_free_pages(free_block_pointers[a.package][a.die]) && get_num_free_blocks(a.package, a.die) > 1) {
		free_block_pointers[a.package][a.die] = find_free_unused_block(a.package, a.die, event.get_current_time());
		if (has_free_pages(free_block_pointers[a.package][a.die])) {
			Free_Space_Per_LUN_Meter::mark_new_space(a, event.get_current_time());
		}
	}
}



void bm_gc_locality::check_if_should_trigger_more_GC(Event const& event) {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(free_block_pointers[i][j]) && partially_used_blocks[i][j].size() > 0) {
				Address pointer = partially_used_blocks[i][j].front();
				partially_used_blocks[i][j].pop();
				free_block_pointers[i][j] = pointer;
				printf("made swap!\n");
			}
			/*else if (!has_free_pages(free_block_pointers[i][j]) ) {
				migrator->schedule_gc(event.get_current_time(), i, j, -1, -1);
			}*/
			else if (get_num_free_blocks(i, j) + partially_used_blocks[i][j].size() <= GREED_SCALE) {
				migrator->schedule_gc(event.get_current_time(), i, j, -1, -1);
			}
		}
	}
	//printf("num partially free blocks:  %d\n", get_num_partially_empty_blocks());
	//Block_manager_parent::check_if_should_trigger_more_GC(event);
}

Address bm_gc_locality::choose_best_address(Event& write) {

	if (write.get_id() == 1047086) {
		int i = 0;
		i++;
	}

	if (!write.is_garbage_collection_op()) {
		return get_free_block_pointer_with_shortest_IO_queue();
	}
	if (write.get_replace_address().valid == NONE) {
		ftl->set_replace_address(write);
	}
	Address block_addr = write.get_replace_address();
	Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
	assert(pointers_for_ongoing_gc_operations.count(block) == 1);
	Address& pointer = pointers_for_ongoing_gc_operations.at(block);
	assert(has_free_pages(pointer));
	return pointer;
}

int bm_gc_locality::get_num_partially_empty_blocks() const {
	int num = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			num += partially_used_blocks[i][j].size();
		}
	}
	return num;
}

Address bm_gc_locality::choose_any_address(Event const& write) {
	write.print();

	/*for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			free_block_pointers[i][j].print();
			printf("\n");
		}
	}
	printf("num par free: %d\n", get_num_partially_empty_blocks());
*/
	return get_free_block_pointer_with_shortest_IO_queue();
}
