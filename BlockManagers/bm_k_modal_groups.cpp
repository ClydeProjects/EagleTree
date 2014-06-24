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
	group::mapping_pages_to_groups =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
}

Block_Manager_Groups::~Block_Manager_Groups() {
	printf("final printing:\n");
	print();
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
		groups[i].prob = msg.groups[i].update_frequency / 100.0;
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
	vector<group_def> new_groups = msg.groups;
	assert(new_groups.size() > 0);
	int offset = 0;
	for (auto gr : new_groups) {
		group g(gr.update_frequency / 100.0, (gr.size / 100.0) * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR, this, ssd);
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

int Block_Manager_Groups::which_group_does_this_page_belong_to(Event const& event) const {
	int la = event.get_logical_address();
	if (group::mapping_pages_to_groups.at(la) != UNDEFINED) {
		return group::mapping_pages_to_groups[la];
	}
	if (event.get_tag() != UNDEFINED) {
		return event.get_tag();
	}
	for (int i = 0; i < groups.size(); i++) {
		if (la >= groups[i].offset && la < groups[i].offset + groups[i].size) {
			return i;
		}
	}
	return UNDEFINED;
}

void Block_Manager_Groups::register_write_outcome(Event const& event, enum status status) {
	int package = event.get_address().package;
	int die = event.get_address().die;

	if (event.get_logical_address() == 22938) {
		int i = 0;
		i++;
	}

	Block_manager_parent::register_write_outcome(event, status);
	int prior_group_id = which_group_does_this_page_belong_to(event);
	int la = event.get_logical_address();
	int new_group_id = event.get_tag() != UNDEFINED ? event.get_tag() : group::mapping_pages_to_groups.at(la);

	if (group::mapping_pages_to_groups.at(la) == UNDEFINED && event.get_tag() != UNDEFINED) {
		group::mapping_pages_to_groups[la] = new_group_id = event.get_tag();
		groups[new_group_id].num_pages++;
	}
	else if (group::mapping_pages_to_groups.at(la) == UNDEFINED && event.get_tag() == UNDEFINED) {
		group::mapping_pages_to_groups[la] = new_group_id = prior_group_id;
		groups[new_group_id].num_pages++;
	}
	else if (prior_group_id != new_group_id) {
		groups[prior_group_id].num_pages--;
		groups[new_group_id].num_pages++;
		group::mapping_pages_to_groups[la] = new_group_id;
	}

	assert(new_group_id < groups.size());
	//assert(prior_group_id < groups.size());

	if (groups[new_group_id].num_pages > groups[new_group_id].size * 1.01) {
		printf("regrouping!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
		}
		groups = group::iterate(groups);
		print();
		//group::init_stats(groups);
	}

	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].free_blocks.blocks[package][die].compare(event.get_address()) > BLOCK) {
			groups[i].free_blocks.register_completion(event);
			if (i != new_group_id) {
				stats.num_group_misses++;
			}
			if (event.get_address().page == BLOCK_SIZE - 1) {
				handle_block_out_of_space(event, i);
			}
		}
	}

	if (event.is_original_application_io()) {
		groups[new_group_id].stats.num_writes_to_group++;
	}
	else {
		groups[new_group_id].stats.num_gc_writes_to_group++;
	}
	groups[new_group_id].stats_gatherer.register_completed_event(event);

	if (event.get_address().page == 0) {
		groups[new_group_id].num_blocks_ever_given[event.get_address().package][event.get_address().die]++;
	}

	groups[new_group_id].num_pages_per_die[event.get_address().package][event.get_address().die]++;
	if (event.get_replace_address().valid == PAGE) {
		groups[new_group_id].num_pages_per_die[event.get_replace_address().package][event.get_replace_address().die]--;
		assert(groups[new_group_id].num_pages_per_die[event.get_replace_address().package][event.get_replace_address().die] >= 0);
	}
	//register_logical_address(event, group_id);

	groups[new_group_id].stats_gatherer.register_completed_event(event);

	if (event.get_id() == 734003) {

		group::print(groups);
		//group::init_stats(groups);
		//StateVisualiser::print_page_status();
	}
	static int count = 0;
	if (event.get_id() > 734003 && event.is_original_application_io() && count++ % 100000 == 0) {
		printf("regrouping!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
		}
		groups = group::iterate(groups);
		print();
		//group::init_stats(groups);
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
	if (event.get_id() == 442555) {
		int i = 0;
		i++;
	}
	bool enough_free_blocks = try_to_allocate_block_to_group(group_id, package, die, event.get_current_time());
	if (!enough_free_blocks) {
		Block* b = groups[group_id].get_gc_victim(package, die);
		if (b != NULL) {
			Address block_addr = Address(b->get_physical_address() , BLOCK);
			if (block_addr.package == 0 && block_addr.die == 0 && block_addr.block == 718) {
				int i = get_num_free_blocks();
				i++;
			}
			migrator->schedule_gc(event.get_current_time(), package, die, block_addr.block, UNDEFINED);
		}
	}
}

bool Block_Manager_Groups::may_garbage_collect_this_block(Block* block) {
	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			group_id = i;
		}
	}
	assert(group_id != UNDEFINED);
	if (groups[group_id].blocks_being_garbage_collected.count(block) == 1) {
		return false;
	}
	bool need_more_blocks = groups[group_id].needs_more_blocks();
	bool starved = groups[group_id].is_starved();
	//bool equib = groups[group_id].in_equilbirium();
	bool equib = group::in_total_equilibrium(groups, group_id);
	int num_active_blocks = groups[group_id].free_blocks.get_num_free_blocks();
	int num_blocks_being_gc = groups[group_id].blocks_being_garbage_collected.size();

	if (!equib && !need_more_blocks && num_active_blocks + num_blocks_being_gc >= SSD_SIZE * PACKAGE_SIZE ) {
		//print();
		return false;
	}

	if (!equib && !need_more_blocks && group_id == 1) {
		//printf("%d   %d   %d\n", group_id, num_active_blocks, num_blocks_being_gc);
		//groups[group_id].print();
	}

	groups[group_id].blocks_being_garbage_collected.insert(block);
	/*if (groups[0].size > 500000) {
		Address a = Address(block->get_physical_address(), BLOCK);
		cout << "group: " << group_id << "   pages:  " <<  block->get_pages_valid() << "\tp:" << a.package << "\td:" << a.die << endl;
	}*/
	return true;
}

bool Block_Manager_Groups::try_to_allocate_block_to_group(int group_id, int package, int die, double time) {
	bool starved = groups[group_id].is_starved();

	bool needs_more_blocks = groups[group_id].needs_more_blocks();
	int actual_size = groups[group_id].block_ids.size() * BLOCK_SIZE;
	int OP = groups[group_id].OP;
	int size = groups[group_id].size;
	int num_free_blocks = get_num_free_blocks(package, die);

	/*if (groups[group_id].stats.num_pages_per_die[package][die] * 0.95 > groups[group_id].get_min_pages_per_die()) {
		//printf("%d    %f\n", groups[group_id].stats.num_pages_per_die[package][die], groups[group_id].get_avg_pages_per_die());
		return true;
	}*/

	if (needs_more_blocks || starved || num_free_blocks > 1) {
		if (!has_free_pages(groups[group_id].free_blocks.blocks[package][die])) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
			}
		}
		if (!has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && !starved) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].next_free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
			}
		}

	}
	//printf("gc in group since it is starved. group %d \n", group_id);

	/*if (num_free_blocks > 10) {
		return true;
	}*/
	//if (needs_more_blocks || groups[group_id].in_equilbirium()) {
	if (needs_more_blocks || group::in_total_equilibrium(groups, group_id)) {
		//printf("group %d needs more blocks  %d\n", group_id, has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && has_free_pages(groups[group_id].free_blocks.blocks[package][die]));
		return has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && has_free_pages(groups[group_id].free_blocks.blocks[package][die]);
	}
	bool still_starved = groups[group_id].is_starved();

	//printf("group %d starved\n", group_id);
	return !still_starved;
}

void Block_Manager_Groups::register_erase_outcome(Event const& event, enum status status) {
	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	//PRINT_LEVEL = 1;
	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			//printf("block erased from group %d\n", i);
			assert(groups[i].block_ids.count(block) == 1);
			groups[i].block_ids.erase(block);
			groups[i].blocks_being_garbage_collected.erase(block);
			groups[i].stats.num_gc_in_group++;
			groups[i].stats_gatherer.register_completed_event(event);
			group_id = i;
		}
	}
	if (group_id == UNDEFINED) {
		event.print();
		printf("block id:  %d\n", event.get_address().get_block_id());
		printf("phyz addr:  %d\n", event.get_address().get_block_id() * BLOCK_SIZE);
		assert(false);
	}
	if (group_id == 0) {
		int i = get_num_free_blocks();
		i++;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	if (rand() % 5 == 0) {
		group::register_erase_completion(groups);
	}
}

void Block_Manager_Groups::check_if_should_trigger_more_GC(double start_time) {
	vector<int> order_group = Random_Order_Iterator::get_iterator(groups.size());
 	for (auto g : order_group) {
 		vector<int> order_packages = Random_Order_Iterator::get_iterator(SSD_SIZE);
		for (auto p : order_packages) {
			vector<int> order_dies = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
			for (auto d : order_dies) {
				bool enough_free_blocks = try_to_allocate_block_to_group(g, p, d, start_time);
				if (!enough_free_blocks) {
					Block* b = groups[g].get_gc_victim(p, d);
					if (b != NULL) {
						Address block_addr = Address(b->get_physical_address() , BLOCK);
						migrator->schedule_gc(start_time, p, d, block_addr.block, UNDEFINED);
					}
				}
			}
		}
	}
}

// handle garbage_collection case. Based on range.
Address Block_Manager_Groups::choose_best_address(Event& write) {
	int group_id = write.get_tag() != UNDEFINED ? write.get_tag() : which_group_does_this_page_belong_to(write);
	return groups[group_id].free_blocks.get_best_block(this);
}

Address Block_Manager_Groups::choose_any_address(Event const& write) {
	/*Address a = get_free_block_pointer_with_shortest_IO_queue();
	if (has_free_pages(a)) {
		assert(false);
		return a;
	}*/
	static int counter = 0;
	if (++counter % 1000000 == 0) {
		printf("stuck in choose any addr\n");
		//print();
	}
	vector<int> order_group = Random_Order_Iterator::get_iterator(groups.size());
	for (auto g : order_group) {
		Address a = groups[g].free_blocks.get_best_block(this);
		if (has_free_pages(a)) {
			return a;
		}
	}
	return Address();
}

bool Block_Manager_Groups::is_in_equilibrium() const {
	for (auto g : groups) {
		if (!g.in_equilbirium()) {
			return false;
		}
	}
	return true;
}

void Block_Manager_Groups::print() {
	printf(".........................................\n");
	group::print(groups);
	printf("num group misses: %d\n", stats.num_group_misses);
	printf("num free blocks in SSD:  %d\n", get_num_free_blocks());
	printf("num gc scheduled  %d\n", migrator->how_many_gc_operations_are_scheduled());
	printf("\n");
}
