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

Block_Manager_Groups::Block_Manager_Groups()
: Block_manager_parent(), stats()
{
	GREED_SCALE = 0;
}

void Block_Manager_Groups::init(Ssd* ssd, FtlParent* ftl, IOScheduler* sched, Garbage_Collector* gc, Wear_Leveling_Strategy* wl, Migrator* m) {
	Block_manager_parent::init(ssd, ftl, sched, gc, wl, m);
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			Address a = free_block_pointers[i][j];
			free_blocks[i][j][0].push_back(a);
			free_block_pointers[i][j] = Address();
		}
	}
}

void Block_Manager_Groups::change_update_frequencies(Groups_Message const& msg) {
	for (int i = 0; i < msg.groups.size(); i++) {
		groups[i].prob = msg.groups[i].first / 100.0;
	}
	vector<group> opt_groups = group::iterate(groups);
	groups = opt_groups;
	group::init_stats(groups);
	printf("\n\n-------------------------------------------------------------------------\n\n");
	print();

	//PRINT_LEVEL = 1;
}

void Block_Manager_Groups::receive_message(Event const& message) {
	assert(message.get_event_type() == MESSAGE);
	Groups_Message const& msg = dynamic_cast<const Groups_Message&>(message);
	if (msg.redistribution_of_update_frequencies) {
		change_update_frequencies(msg);
		return;
	}
	vector<pair<int, int> > new_groups = msg.groups;
	assert(new_groups.size() > 0);
	int offset = 0;
	for (auto gr : new_groups) {
		group g(gr.first / 100.0, (gr.second / 100.0) * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR, this, ssd);
		//g.free_blocks.find_free_blocks(this, message.get_current_time());
		g.offset = offset;
		offset += g.size;
		groups.push_back(g);
	}

	vector<group> opt_groups = group::iterate(groups);
	group::print(opt_groups);
	double gen_write_amp = group::get_average_write_amp(groups);
	double opt_write_amp = group::get_average_write_amp(opt_groups);
	double diff = abs(gen_write_amp - opt_write_amp) / opt_write_amp;
	cout << "approx: " << gen_write_amp << endl;
	cout << "optimal: " << opt_write_amp << endl;
	cout << "% diff: " << diff << endl;
	groups = opt_groups;
}

void Block_Manager_Groups::register_write_arrival(Event const& e) {}

int Block_Manager_Groups::which_group_does_this_page_belong_to(Event const& event) {
	for (int i = 0; i < groups.size(); i++) {
		int la = event.get_logical_address();
		if (la >= groups[i].offset && la < groups[i].offset + groups[i].size) {
			return i;
		}
	}
	assert(false);
	return UNDEFINED;
}

void Block_Manager_Groups::register_write_outcome(Event const& event, enum status status) {

	if (event.get_application_io_id() == 926630) {
		int i = 0;
		i++;
		//event->print();
	}
	int package = event.get_address().package;
	int die = event.get_address().die;

	Block_manager_parent::register_write_outcome(event, status);
	int group_id = which_group_does_this_page_belong_to(event);

	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].free_blocks.blocks[package][die].compare(event.get_address()) > BLOCK) {
			groups[i].free_blocks.register_completion(event);
			if (i != group_id) {
				stats.num_group_misses++;
			}
		}
	}

	if (event.is_original_application_io()) {
		groups[group_id].stats.num_writes_to_group++;
	}
	if (!event.is_original_application_io()) {
		groups[group_id].stats.num_gc_writes_to_group++;
	}

	if (event.get_address().page == BLOCK_SIZE - 1) {
		handle_block_out_of_space(event, group_id);
	}

	if (event.get_id() == 734003) {
		group::print(groups);
		group::init_stats(groups);
		//StateVisualiser::print_page_status();
	}
	static int count = 0;
	if (event.get_id() > 734003 && count++ % 10000 == 0) {
		print();
		//StateVisualiser::print_page_status();
	}

}

void Block_Manager_Groups::handle_block_out_of_space(Event const& event, int group_id) {
	int package = event.get_address().package;
	int die = event.get_address().die;

	if (has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) &&
			has_free_pages(groups[group_id].free_blocks.blocks[package][die])) {
		return;
	}
	if (has_free_pages(groups[group_id].next_free_blocks.blocks[package][die])) {
		groups[group_id].free_blocks.blocks[package][die] = groups[group_id].next_free_blocks.blocks[package][die];
		groups[group_id].next_free_blocks.blocks[package][die] = Address();
	}
	bool enough_free_blocks = try_to_allocate_block_to_group(group_id, package, die, event.get_current_time());
	if (!enough_free_blocks) {
		Block* b = groups[group_id].get_gc_victim(package, die);
		if (b != NULL) {
			Address block_addr = Address(b->get_physical_address() , BLOCK);

			migrator->schedule_gc(event.get_current_time(), package, die, block_addr.block, UNDEFINED);
		}
	}
}

bool Block_Manager_Groups::try_to_allocate_block_to_group(int group_id, int package, int die, double time) {
	if (groups[group_id].is_starved()) {
		printf("starved!\n");
	}
	if (groups[group_id].block_ids.size() * BLOCK_SIZE <= groups[group_id].OP + groups[group_id].size || groups[group_id].is_starved()) {
		if (!has_free_pages(groups[group_id].free_blocks.blocks[package][die])) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
			}
		}
		if (!has_free_pages(groups[group_id].next_free_blocks.blocks[package][die])) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].next_free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
			}
		}
	}
	return has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && has_free_pages(groups[group_id].free_blocks.blocks[package][die]);
}

void Block_Manager_Groups::register_erase_outcome(Event const& event, enum status status) {
	//group::print(groups);

	if (event.get_address().package == 0 && event.get_address().die == 0 && event.get_address().block == 285) {
		//event.print();
	}

	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	/*if (block->get_physical_address() == 185856) {
		int i = 0;
		i++;
	}*/


	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			//printf("block erased from group %d\n", i);
			groups[i].block_ids.erase(block);
			groups[i].stats.num_gc_in_group++;
			group_id = i;
		}
	}
	if (group_id == UNDEFINED) {
		event.print();
		assert(false);
	}
	/*printf("erased block from group %d  ", group_id);
	event.get_address().print();
	printf("\n");*/
	Block_manager_parent::register_erase_outcome(event, status);
	//try_to_allocate_block_to_group(group_id, a.package, a.die, event.get_current_time());
	// see which groups need the free space, and give it to it.
}

void Block_Manager_Groups::check_if_should_trigger_more_GC(double start_time) {


 	for (int i = 0; i < groups.size(); i++) {
		for (int p = 0; p < SSD_SIZE; p++) {
			for (int d = 0; d < PACKAGE_SIZE; d++) {
				bool enough_free_blocks = try_to_allocate_block_to_group(i, p, d, start_time);
				if (!enough_free_blocks) {
					Block* b = groups[i].get_gc_victim(p, d);
					if (b != NULL) {
						Address block_addr = Address(b->get_physical_address() , BLOCK);
						migrator->schedule_gc(start_time, p, d, block_addr.block, UNDEFINED);
					}
				}
			}
		}
	}

	//printf("check_if_should_trigger_more_GC\n");
}

// handle garbage_collection case. Based on range.
Address Block_Manager_Groups::choose_best_address(Event& write) {
	int group_id = which_group_does_this_page_belong_to(write);
	return groups[group_id].free_blocks.get_best_block(this);
}

Address Block_Manager_Groups::choose_any_address(Event const& write) {
	/*Address a = get_free_block_pointer_with_shortest_IO_queue();
	if (has_free_pages(a)) {
		assert(false);
		return a;
	}*/
	for (auto g : groups) {
		Address a = g.free_blocks.get_best_block(this);
		if (has_free_pages(a)) {
			return a;
		}
	}
	return Address();
}

void Block_Manager_Groups::print() {
	printf("num group misses: %d\n", stats.num_group_misses);
	group::print(groups);
	printf("num free blocks in SSD:  %d\n", get_num_free_blocks());
	printf("num gc scheduled  %d\n", migrator->how_many_gc_operations_are_scheduled());
}
