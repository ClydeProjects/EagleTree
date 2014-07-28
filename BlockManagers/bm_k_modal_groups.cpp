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

int Block_Manager_Groups::detector_type = 0;
int Block_Manager_Groups::reclamation_threshold = 0;
int Block_Manager_Groups::starvation_threshold = 0;
bool Block_Manager_Groups::balancing_policy_on = true;
bool Block_Manager_Groups::reserve_blocks_on = true;

Block_Manager_Groups::Block_Manager_Groups()
: Block_manager_parent(), stats(), groups(), detector(NULL)
{
	GREED_SCALE = 0;
}

Block_Manager_Groups::~Block_Manager_Groups() {
	printf("final printing:\n");
	print();
}

void Block_Manager_Groups::init_detector() {
	if (detector != NULL) {
		delete detector;
	}
	if (detector_type == 0) {
		detector = new tag_detector(groups);
	}
	else if (detector_type == 1) {
		detector = new adaptive_bloom_detector(groups, this);
	}
	else {
		detector = new non_adaptive_bloom_detector(groups, this);
	}
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
	assert(get_num_free_blocks() == SSD_SIZE * PACKAGE_SIZE * PLANE_SIZE);
	group::mapping_pages_to_groups =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
	group::mapping_pages_to_tags =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
	init_detector();
	group new_group(1, NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR, this, ssd, 0);
	groups.push_back(new_group);
	detector->change_in_groups(groups, 0);

}

void Block_Manager_Groups::change_update_frequencies(Groups_Message const& msg) {
	for (int i = 0; i < msg.groups.size(); i++) {
		groups[i].prob = groups[i].actual_prob = msg.groups[i].update_frequency / 100.0;
	}
	vector<group> opt_groups = group::iterate(groups);
	groups = opt_groups;
	group::init_stats(groups);
	printf("\n\n-------------------------------------------------------------------------\n\n");
	print();
}

void Block_Manager_Groups::receive_message(Event const& message) {
	assert(message.get_event_type() == MESSAGE);
	Groups_Message const& msg = dynamic_cast<const Groups_Message&>(message);
	if (msg.redistribution_of_update_frequencies) {
		change_update_frequencies(msg);
		return;
	}
	vector<group_def> new_groups = msg.groups;
	if (new_groups.size() == 0) {
		return;
	}
	for (int i = 0; i < groups.size(); i++) {
		groups[i].retire_active_blocks(message.get_current_time());
	}
	groups.clear();
	double offset = 0;
	for (int i = 0; i < new_groups.size(); i++) {
		double update_prob = new_groups[i].update_frequency / 100.0;
		double size = (new_groups[i].size / 100.0) * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
		group g(update_prob, size, this, ssd, i);
		//g.free_blocks.find_free_blocks(this, message.get_current_time());
		g.offset = offset;
		offset += g.size;
		groups.push_back(g);
	}
	printf("%d   %d\n", new_groups.size(), groups.size());
	group::print(groups);
	printf("\n\n");
	vector<group> opt_groups = group::iterate(groups);
	group::print(opt_groups);
	double gen_write_amp = group::get_average_write_amp(groups);
	double opt_write_amp = group::get_average_write_amp(opt_groups);
	double diff = abs(gen_write_amp - opt_write_amp) / opt_write_amp;
	cout << "approx: " << gen_write_amp << endl;
	cout << "optimal: " << opt_write_amp << endl;
	cout << "% diff: " << diff << endl;
	groups = opt_groups;
	init_detector();
}

void Block_Manager_Groups::register_write_arrival(Event const& e) {}

int tag_detector::which_group_should_this_page_belong_to(Event const& event) {
	if (event.get_tag() != UNDEFINED) {
		return event.get_tag();
	}
	int la = event.get_logical_address();
	for (int i = 0; i < groups.size(); i++) {
		if (la >= groups[i].offset && la < groups[i].offset + groups[i].size + i) {
			return i;
		}
	}
	printf("no group for event ");
	event.print();
	return UNDEFINED;
}

void Block_Manager_Groups::register_write_outcome(Event const& event, enum status status) {
	int package = event.get_address().package;
	int die = event.get_address().die;

	Block_manager_parent::register_write_outcome(event, status);

	int la = event.get_logical_address();
	int prior_group_id = group::mapping_pages_to_groups.at(la);
	int ideal_group_id = detector->which_group_should_this_page_belong_to(event);

	/*if (ideal_group_id < 0 || ideal_group_id >= groups.size()) {
		printf("group_id  %d\n", ideal_group_id);
		event.print();
	}*/
	assert(ideal_group_id != UNDEFINED && ideal_group_id < groups.size());

	int actual_new_group = UNDEFINED;
	// The page might have been written on a different group due to a lack of space
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].free_blocks.blocks[package][die].compare(event.get_address()) > BLOCK) {
			groups[i].free_blocks.register_completion(event);
			if (i != ideal_group_id) {
				stats.num_group_misses++;
			}
			if (event.get_address().page == BLOCK_SIZE - 1) {
				handle_block_out_of_space(event, i);
			}
			actual_new_group = i;
		}
	}

	if (prior_group_id == UNDEFINED) {
		group::mapping_pages_to_groups[la] = ideal_group_id;
		groups[ideal_group_id].num_pages++;
	}
	else if (prior_group_id != ideal_group_id) {
		groups[prior_group_id].num_pages--;
		groups[ideal_group_id].num_pages++;
		groups[ideal_group_id].stats.migrated_in++;
		groups[prior_group_id].stats.migrated_out++;
		group::mapping_pages_to_groups[la] = ideal_group_id;
	}

	groups[ideal_group_id].num_pages_per_die[event.get_address().package][event.get_address().die]++;
	if (event.get_replace_address().valid == PAGE) {
		groups[prior_group_id].num_pages_per_die[event.get_replace_address().package][event.get_replace_address().die]--;
		assert(groups[prior_group_id].num_pages_per_die[event.get_replace_address().package][event.get_replace_address().die] >= 0);
	}

	/*if (actual_new_group != ideal_group_id) {
		printf("alert!\n");
	}*/



	groups[ideal_group_id].register_write_outcome(event);

	if (event.is_original_application_io()) {
		groups[ideal_group_id].stats.num_writes_to_group++;
	}
	else if (event.is_garbage_collection_op()) {
		groups[prior_group_id].stats.num_gc_writes_to_group++;
	}

	detector->register_write_completed(event, prior_group_id, ideal_group_id);

	if (groups[ideal_group_id].num_pages > groups[ideal_group_id].size * 1.05 || groups[ideal_group_id].actual_prob > groups[ideal_group_id].prob * 1.05) {
		//printf("regrouping due to change!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
			groups[i].prob = groups[i].actual_prob;
		}
		groups = group::iterate(groups);
		group::num_writes_since_last_regrouping = 0;
		//print();
		//group::init_stats(groups);
		//StatisticsGatherer::get_global_instance()->print();
		//StatisticsGatherer::init();
	}
	group::num_writes_since_last_regrouping++;

	/*if (event.get_id() == NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR || event.get_id() == 2 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR) {
		printf("reseting stats\n");
		group::print(groups);
		group::init_stats(groups);
		StatisticsGatherer::init();
		//StateVisualiser::print_page_status();
	}*/
	static int count = 0;
	int lba = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	if (event.get_id() > lba && event.is_original_application_io() && count++ % 100000 == 0) {
		printf("regrouping!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
			groups[i].prob = groups[i].actual_prob;
		}
		//groups = group::iterate(groups);
		print();
		printf("num writes:  %d   %d\n", StatisticsGatherer::get_global_instance()->total_writes(), count);

		int wga = 3;
		if ( count > 5700000 && count < 5800000) {
			printf("stopping before sudden surge.");
			wga++;
			printf("%d\n", wga);
		}

		double factor_g1, factor_g2;
		if (groups[0].stats.num_gc_in_group == 0 || groups[1].stats.num_gc_in_group == 0) {
			factor_g1 = factor_g2 = 0;
		} else {
			factor_g1 = groups[0].stats.num_gc_writes_to_group / groups[0].stats.num_gc_in_group;
			factor_g2 = groups[1].stats.num_gc_writes_to_group / groups[1].stats.num_gc_in_group;
		}
		int sum_gc = 0;
		for (auto g : groups) {
			sum_gc += g.stats.num_gc_writes_to_group;
		}

		StatisticData::register_statistic("groups_gc", {
				new Integer(count),
				new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
				new Integer(factor_g1),
				new Integer(groups[0].stats.num_gc_writes_to_group),
				new Integer(factor_g2),
				new Integer(groups[1].stats.num_gc_writes_to_group),
				new Integer(sum_gc)
		});

		StatisticData::register_field_names("groups_gc", {
				"count",
				"num_writes",
				"group 1 factor",
				"group 1 gc",
				"group 2 factor",
				"group 2 gc"
				"Total GC"
		});


		  static clock_t time_sig = 0;
		  clock_t time_now = clock();
		  if (time_sig > 0) {
			  double elapsed_secs = double(time_now - time_sig) / CLOCKS_PER_SEC;
			  printf("elapsed_secs:   %f\n", elapsed_secs);
		  }
		time_sig = time_now;

		group::init_stats(groups);
		//stats.num_group_misses = 0;
		//stats = statistics();
		//StatisticsGatherer::get_global_instance()->print();
		//StatisticsGatherer::init();
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
	try_to_allocate_block_to_group(group_id, package, die, event.get_current_time());
}

bool Block_Manager_Groups::may_garbage_collect_this_block(Block* block, double current_time) {
	int group_id = UNDEFINED;
	int ongoing_gc = 0;

	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			group_id = i;
			ongoing_gc += groups[i].blocks_being_garbage_collected.size();
			//break;
		}
	}

	Address addr = Address(block->get_physical_address(), BLOCK);

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

	if (!equib && !need_more_blocks && num_active_blocks + num_blocks_being_gc >= Block_Manager_Groups::reclamation_threshold ) {
		return false;
	}

	if (get_num_free_blocks(addr.package, addr.die) > 1) {
		//printf("triggering gc in %d %d even though there are %d free blocks \n", addr.package, addr.die, get_num_free_blocks(addr.package, addr.die));
		//return false;
	}

	if (block->get_pages_valid() > groups[group_id].get_avg_pages_per_block_per_die()) {
		return false;
	}

	// This lines seems to eliminate the spike in gc during group creation
	// however, it sometimes does cause us to pause indefinitely. Need to fix that.
	int f = 234;

	if (!equib && need_more_blocks && get_num_pages_available_for_new_writes() >= BLOCK_SIZE) {
		if (StatisticsGatherer::get_global_instance()->total_writes() > 2334004) {
			//print();
			int pointers_group1 = groups[0].free_blocks.get_num_free_blocks();
			int pointers_group2 = groups[1].free_blocks.get_num_free_blocks();
			//printf("cancelling.  ongoing gc: %d   pointers1: %d   pointers2: %d   pages for new writes:  %d",
			//		ongoing_gc, pointers_group1, pointers_group2, get_num_available_pages_for_new_writes());
			f++;
			//printf("f\n", f);
		}
		return false;
	}

	if (block->get_pages_valid() > groups[group_id].size / groups[group_id].block_ids.size()) {
		//printf("working\n");
		//return false;
	}

	/*double over_prov = (groups[group_id].block_ids.size() * BLOCK_SIZE - groups[group_id].size) / groups[group_id].size;
	double expected_num_migrations = exp(- 0.9 * over_prov) / (over_prov + 1);
	expected_num_migrations *= BLOCK_SIZE;

	if (block->get_pages_valid() > expected_num_migrations * 1.5) {
		printf("working\n");
		return false;
	}*/

	/*if (group_id == 1) {
		double over_prov = (groups[group_id].block_ids.size() * BLOCK_SIZE - groups[group_id].size) / groups[group_id].size;
		double expected_num_migrations = exp(- 0.9 * over_prov) / (over_prov + 1);
		expected_num_migrations *= BLOCK_SIZE;
		printf("%d %d: valid pages: %d   expected: %f  num free blocks in lun:  %d    num group blocks in LUN: %d    in equib1:   %d  in equib2  %d     total pages: %d   num blocks: %d \n", addr.package, addr.die,
				block->get_pages_valid(), expected_num_migrations, get_num_free_blocks(addr.package, addr.die), groups[group_id].blocks_queue_per_die[addr.package][addr.die].size(), groups[group_id].in_equilbirium(),
				groups[0].in_equilbirium(), groups[group_id].num_pages, groups[group_id].block_ids.size());
		int blocks_needed = (groups[group_id].OP + groups[group_id].size) / BLOCK_SIZE - groups[group_id].block_ids.size();
		printf("total free blocks in SSD: %d   group 0:  %d    group 1:  %d    needs block: %d\n", get_num_free_blocks(), groups[0].block_ids.size(), groups[1].block_ids.size(), blocks_needed);
		printf("pages per block in this lun:  %f, and on avg  %f\n", groups[group_id].num_pages_per_die[addr.package][addr.die] / groups[group_id].num_blocks_per_die[addr.package][addr.die], groups[group_id].get_avg_pages_per_block_per_die());
		groups[group_id].print_blocks_valid_pages();
		groups[group_id].print_blocks_valid_pages_per_die();
	}*/


	groups[group_id].blocks_being_garbage_collected.insert(block);
	assert(groups[group_id].blocks_being_garbage_collected.size() <= SSD_SIZE * PACKAGE_SIZE);

	StatisticData::register_statistic("gc_for_diff_groups", {
			new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
			new Integer(group_id),
			new Integer(block->get_pages_valid())
	});

	StatisticData::register_field_names("gc_for_diff_groups", {
			"num_writes",
			"group",
			"num_pages_to_migrate"
	});

	return true;
}

void Block_Manager_Groups::give_block_to_group(int package, int die, int group_id, double current_time) {
	Address block_addr = find_free_unused_block(package, die, current_time);
	if (has_free_pages(block_addr)) {
		groups[group_id].accept_block(block_addr);
	}
}

void Block_Manager_Groups::try_to_allocate_block_to_group(int group_id, int package, int die, double time) {
	bool starved = groups[group_id].is_starved();
	bool needs_more_blocks = groups[group_id].needs_more_blocks();
	int actual_size = groups[group_id].block_ids.size() * BLOCK_SIZE;
	int OP = groups[group_id].OP;
	int size = groups[group_id].size;
	int num_free_blocks = get_num_free_blocks(package, die);
	bool equib = groups[group_id].in_equilbirium();
	bool has_free_block = has_free_pages(groups[group_id].free_blocks.blocks[package][die]);
	bool has_reserve_block = has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]);
	if (!has_free_block && (needs_more_blocks || starved || num_free_blocks > 1 || equib)) {
		give_block_to_group(package, die, group_id, time);
		has_free_block = has_free_pages(groups[group_id].free_blocks.blocks[package][die]);
	}
	if (!has_reserve_block && (needs_more_blocks || num_free_blocks > 1 || equib)) {
		give_block_to_group(package, die, group_id, time);
		has_reserve_block = has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]);
	}

	/*if (group_id == 1 && groups[group_id].get_avg_pages_per_block_per_die() > groups[group_id].num_pages_per_die[package][die] / groups[group_id].num_blocks_per_die[package][die]) {
		printf()
	}*/

	if ((!has_free_block || !has_reserve_block) && (needs_more_blocks || group::in_total_equilibrium(groups, group_id))) {
		stats.num_normal_gc_operations_requested++;
		request_gc(group_id, package, die, time);
	}
	else if (groups[group_id].is_starved()) {
		stats.num_starved_gc_operations_requested++;
		request_gc(group_id, package, die, time);
	}
}

void Block_Manager_Groups::trigger_gc_in_same_lun_but_different_group(int package, int die, int group_id, double time) {
	if (get_num_free_blocks(package, die) > 1) {
		return;
	}
	// find the group with the highest excess of blocks

	int max_excess_blocks_needed = 0;
	int selected_group = UNDEFINED;
	// identify a group with too many pages
	for (int i = 0; i < groups.size(); i++ ) {
		int num_excess_blocks = groups[i].OP + groups[i].size - (groups[i].block_ids.size() * BLOCK_SIZE);
		selected_group = i;
		if (num_excess_blocks > max_excess_blocks_needed) {
			selected_group = i;
			max_excess_blocks_needed = num_excess_blocks;
		}
 	}
	if (!groups[selected_group].in_equilbirium() && !groups[selected_group].needs_more_blocks()) {

	}
	request_gc(selected_group, package, die, time);
}

void Block_Manager_Groups::register_erase_outcome(Event const& event, enum status status) {
	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			//printf("block erased from group %d\n", i);
			groups[i].register_erase_outcome(event);
			group_id = i;
		}
	}
	if (group_id == UNDEFINED) {
		event.print();
		printf("block id:  %d\n", event.get_address().get_block_id());
		printf("phyz addr:  %d\n", event.get_address().get_block_id() * BLOCK_SIZE);
		assert(false);
	}
	Block_manager_parent::register_erase_outcome(event, status);
	group::count_num_groups_that_need_more_blocks(groups);
}

void Block_Manager_Groups::check_if_should_trigger_more_GC(Event const& event) {

	// Find the group with the fewest blocks in the LUN on which the erase was made
	/*Address a = event.get_address();
	int min_num_blocks = BLOCK_SIZE;
	int selected_group = UNDEFINED;
	for (int i = 0; i < groups.size(); i++ ){
		int num_blocks = groups[i].num_blocks_per_die[a.package][a.die];
		int num_pages = groups[i].num_pages_per_die[a.package][a.die];
		if (num_blocks > 0 && num_pages / num_blocks < min_num_blocks) {
			min_num_blocks = num_pages / num_blocks;
			selected_group = i;
		}
	}
	// If the group that was found has fewer blocks on this LUN than it has on average on other LUNs, then give the block to this group.
	if (selected_group != UNDEFINED && balancing_policy_on) {
		double avg = groups[selected_group].get_avg_pages_per_block_per_die();
		if (min_num_blocks * 1.40 < avg) {
			int curr_num_blocks = groups[selected_group].block_ids.size();
			try_to_allocate_block_to_group(selected_group, a.package, a.die, event.get_current_time());
			if (groups[selected_group].block_ids.size() > curr_num_blocks) {
				printf("successfully donated block! \n");
			}
		}
	}*/

	vector<int> order = Random_Order_Iterator::get_iterator(groups.size());
	for (auto g : order) {
		if (!groups[g].in_equilbirium() && !groups[g].needs_more_blocks()) {
			int curr_num_blocks = groups[g].block_ids.size();
			try_to_allocate_block_to_group(g, event.get_address().package, event.get_address().die, event.get_current_time());
			if (groups[g].block_ids.size() > curr_num_blocks) {
				//printf("successfully donated block! \n");
			}
		}
	}

	// Check if more GC should be triggered
	vector<int> order_group = Random_Order_Iterator::get_iterator(groups.size());
 	for (auto g : order_group) {
 		vector<int> order_packages = Random_Order_Iterator::get_iterator(SSD_SIZE);
		for (auto p : order_packages) {
			vector<int> order_dies = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
			for (auto d : order_dies) {
				try_to_allocate_block_to_group(g, p, d, event.get_current_time());
			}
		}
	}
}

void Block_Manager_Groups::request_gc(int group_id, int package, int die, double time) {
	Block* b = groups[group_id].get_gc_victim(package, die);
	if (b != NULL) {
		Address block_addr = Address(b->get_physical_address() , BLOCK);
		migrator->schedule_gc(time, package, die, block_addr.block, UNDEFINED);
	}
}

// handle garbage_collection case. Based on range.
Address Block_Manager_Groups::choose_best_address(Event& write) {
	if (write.is_garbage_collection_op() && write.get_tag() == UNDEFINED) {
		int tag = group::mapping_pages_to_tags.at(write.get_logical_address());
		write.set_tag(tag);
	}
	int group_id = detector->which_group_should_this_page_belong_to(write);
	if (!(group_id >= 0 && group_id < groups.size())) {
		write.print();
	}
	assert(group_id >= 0 && group_id < groups.size());
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
		printf("stuck in choose any addr. num writes  %d\n", StatisticsGatherer::get_global_instance()->total_writes());
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

void Block_Manager_Groups::add_group(double starting_prob_val) {
	group new_group = group(starting_prob_val, BLOCK_SIZE, this, ssd, groups.size());
	groups.push_back(new_group);
}

void Block_Manager_Groups::print() {
	printf(".........................................\n");
	group::print(groups);
	printf("num group misses: %d\n", stats.num_group_misses);
	printf("num normal gc operations requested: %d\n", stats.num_normal_gc_operations_requested);
	printf("num starved gc operations requested: %d\n", stats.num_starved_gc_operations_requested);
	printf("num free blocks in SSD:  %d\n", get_num_free_blocks());
	printf("num gc scheduled  %d\n", migrator->how_many_gc_operations_are_scheduled());
	printf("\n");
}

bloom_detector::bloom_detector(vector<group>& groups, Block_Manager_Groups* bm) :
	temperature_detector(groups), data(), bm(bm), current_interval_counter(get_interval_length()),
	interval_size_of_the_lba_space(0.003), lowest_group(NULL), highest_group(NULL)
{
	for (int i = 0; i < groups.size(); i++) {
		group_data* gd = new group_data(groups[i], groups);
		data.push_back(gd);
	}
}

bloom_detector::bloom_detector() :
	temperature_detector(), data(), bm(NULL), current_interval_counter(0),
	interval_size_of_the_lba_space(), lowest_group(NULL), highest_group(NULL)
{}

void bloom_detector::change_in_groups(vector<group>& groups, double current_time) {
	for (int i = 0; i < data.size(); i++) {
		delete data[i];
	}
	data.clear();
	for (int i = 0; i < groups.size(); i++) {
		group_data* gd = new group_data(groups[i], groups);
		data.push_back(gd);
	}
	current_interval_counter = get_interval_length();
	update_probilities(current_time);
}

int bloom_detector::which_group_should_this_page_belong_to(Event const& event) {
	ulong la = event.get_logical_address();
	int group_id = group::mapping_pages_to_groups[la];

	// first time this logical addrwss is ever written. Write to the least updated group
	if (group_id == UNDEFINED) {
		return lowest_group->index;
	}
	// this early escape is in order to avoid searching the bloom filters, which seem to be a bottleneck
	if (event.is_original_application_io() && data[group_id] == highest_group && !create_higher_group(group_id)) {
		return highest_group->index;
	}

	int num_occurances_in_filters = 0;
	num_occurances_in_filters += data[group_id]->current_filter.contains(la);

	// no need to look at the other filters. Page is staying in current group.
	if (num_occurances_in_filters == 0 && event.is_original_application_io()) {
		return group_id;
	}
	else if (num_occurances_in_filters == 1 && !event.is_original_application_io()) {
		return group_id;
	}
	num_occurances_in_filters += data[group_id]->filter2.contains(la);
	if (num_occurances_in_filters == 1) {
		return group_id;
	}
	num_occurances_in_filters += data[group_id]->filter3.contains(la);

	// create new groups if needed
	if (false && num_occurances_in_filters == 0 &&
			data[group_id]->lower_group_id == UNDEFINED && groups[group_id].num_pages > 10 * BLOCK_SIZE &&
			!event.is_original_application_io() && data[group_id]->age_in_group_periods >= 3 && data[group_id]->age_in_intervals > 100 &&
			data[group_id] == lowest_group) {
		group_data* next_coldest = data[lowest_group->upper_group_id];
		if (data.size() == 1 || lowest_group->get_hits_per_page() * 2 < next_coldest->get_hits_per_page()) {
			if (data.size() > 1) {
				printf("creating new coldest group. coldest is: %d with %f and next is %d with %f\n", lowest_group->index, lowest_group->get_hits_per_page() * 1000, next_coldest->index, next_coldest->get_hits_per_page() * 1000);
			}
			group::print(groups);
			printf("num writes so far:  %d\n", StatisticsGatherer::get_global_instance()->total_writes());
			bm->add_group(0);
			group const& new_group = groups.back();
			group_data* gd = new group_data(new_group, groups);
			data.push_back(gd);
			data[group_id]->lower_group_id = data.size() - 1;
			data[group_id]->age_in_intervals = 0;
			data[group_id]->age_in_group_periods = 0;
			gd->upper_group_id = group_id;
			gd->lower_group_id = UNDEFINED;
			lowest_group = gd;
		}
	}
	else if (/*StatisticsGatherer::get_global_instance()->total_writes() > 5000000 &&*/
			data.size() < 2 &&
			num_occurances_in_filters == 2 && event.is_original_application_io() && create_higher_group(group_id)) {
		group_data* next_hottest = data[highest_group->lower_group_id];
		if (data.size() > 1) {
			printf("creating new hot group. hottest is: %d with %f and next is %d with %f\n", highest_group->index, highest_group->get_hits_per_page(), next_hottest->index, next_hottest->get_hits_per_page());
		}
		group::print(groups);
		printf("num writes so far:  %d\n", StatisticsGatherer::get_global_instance()->total_writes());
		bm->add_group(0);
		group const& new_group = groups.back();
		group_data* gd = new group_data(new_group, groups);
		data.push_back(gd);
		data[group_id]->upper_group_id = data.size() - 1;
		data[group_id]->age_in_intervals = 0;
		data[group_id]->age_in_group_periods = 0;
		gd->lower_group_id = group_id;
		gd->upper_group_id = UNDEFINED;
		highest_group = gd;
	}
	if (num_occurances_in_filters == 0 && data[group_id]->lower_group_id != UNDEFINED && !event.is_original_application_io() && rand() % 2 == 0) {
		//printf("Demoting page with tag %d from %d to %d\n", event.get_tag(), group_id, data[group_id]->lower_group_id );
		return data[group_id]->lower_group_id;
	}
	else if (num_occurances_in_filters == 3 && data[group_id]->upper_group_id != UNDEFINED && event.is_original_application_io()) {
		//printf("Promoting page with tag %d from %d to %d\n", event.get_tag(), group_id, data[group_id]->lower_group_id );
		return data[group_id]->upper_group_id;
	}
	return group_id;
}

bool bloom_detector::create_higher_group(int i) const {
	group_data* next_hottest = data[highest_group->lower_group_id];
	bool enough_hit_rate_diff = data.size() == 1 || highest_group->get_hits_per_page() > next_hottest->get_hits_per_page() * 2;
	bool all = data[i]->upper_group_id == UNDEFINED &&
				groups[i].num_pages > 10 * BLOCK_SIZE &&
				data[i]->age_in_group_periods >= 2 &&
				data[i]->age_in_intervals > 100 &&
				data[i] == highest_group &&
				enough_hit_rate_diff;
	return all;
}

void bloom_detector::register_write_completed(Event const& event, int prior_group_id, int new_group_id) {

	if (!event.is_original_application_io()) {
		return;
	}
	if (event.get_logical_address() == 599111) {
		//printf("599111 to group %d\n", new_group_id);
	}

	//assert(!data[group_id]->current_filter.contains(event.get_logical_address()));
	data[new_group_id]->current_filter.insert(event.get_logical_address());
	/*if (prior_group_id != new_group_id) {
		data[new_group_id]->filter2.insert(event.get_logical_address());
	}*/
	data[new_group_id]->interval_hit_count++;

	if (--data[new_group_id]->bloom_filter_hits == 0) {
		group_interval_finished(new_group_id);
	}

	if (--current_interval_counter == 0) {
		update_probilities(event.get_current_time());
	}
}

void bloom_detector::group_interval_finished(int group_id) {
	//printf("group_interval_finished  %d\n", group_id);
	data[group_id]->bloom_filter_hits = groups[group_id].num_pages;
	data[group_id]->filter3 = data[group_id]->filter2;
	data[group_id]->filter2 = data[group_id]->current_filter;

	bloom_parameters params;
	params.false_positive_probability = 0.01;
	params.projected_element_count = groups[group_id].num_pages;
	params.compute_optimal_parameters();
	data[group_id]->current_filter = bloom_filter(params);
	data[group_id]->age_in_group_periods++;
}

void bloom_detector::change_id_for_pages(int old_id, int new_id) {
	for (int i = 0; i < group::mapping_pages_to_groups.size(); i++) {
		if (group::mapping_pages_to_groups[i] == old_id) {
			group::mapping_pages_to_groups[i] = new_id;
		}
	}
}

void bloom_detector::merge_groups(group_data* gd1, group_data* gd2, double current_time) {
	group& g1 = groups[gd1->index];
	group& g2 = groups[gd2->index];
	group::print(groups);
	//printf("age:  %d\n ", gd1->age_in_intervals);
	g1.prob += g2.prob;
	g1.size += g2.size;
	g1.num_pages += g2.num_pages;
	g1.offset = UNDEFINED;
	g1.OP += g2.OP;
	g1.OP_average += g2.OP_average;
	g1.OP_greedy += g2.OP_greedy;
	g1.OP_prob += g2.OP_prob;

	g2.retire_active_blocks(current_time);

	g1.block_ids.insert(g2.block_ids.begin(), g2.block_ids.end());
	g1.blocks_being_garbage_collected.insert(g2.blocks_being_garbage_collected.begin(), g2.blocks_being_garbage_collected.end());
	g1.actual_prob += g2.actual_prob;

	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			g1.num_pages_per_die[i][j] += g2.num_pages_per_die[i][j];
			g1.num_blocks_per_die[i][j] += g2.num_blocks_per_die[i][j];
			g1.num_blocks_ever_given[i][j] += g2.num_blocks_ever_given[i][j];
			g1.blocks_queue_per_die[i][j].insert(g1.blocks_queue_per_die[i][j].end(), g2.blocks_queue_per_die[i][j].begin(), g2.blocks_queue_per_die[i][j].end());
		}
	}

	g1.stats.num_gc_in_group += g2.stats.num_gc_in_group;
	g1.stats.num_writes_to_group += g2.stats.num_writes_to_group;
	g1.stats.num_gc_writes_to_group += g2.stats.num_gc_writes_to_group;
	g1.stats.migrated_in += g2.stats.migrated_in;
	g1.stats.migrated_out += g2.stats.migrated_out;

	change_id_for_pages(g2.index, g1.index);

	// erase the group
	groups.erase(groups.begin() + gd2->index);
	delete data[gd2->index];
	data.erase(data.begin() + gd2->index);

	// update the ids of the groups to match the indicies
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].index != i) {
			printf("changing %d to %d\n", groups[i].index, i);
			change_id_for_pages(groups[i].index, i);
			groups[i].index = i;
			data[i]->index = i;
		}
	}



	group::iterate(groups);
}




bloom_detector::group_data::group_data(group const& group_ref, vector<group>& groups) :
		current_filter(), filter2(), filter3(),
		bloom_filter_hits(group_ref.size),
		interval_hit_count(0),
				lower_group_id(0), upper_group_id(0), index(group_ref.index), groups_none(), groups(groups), age_in_intervals(0), age_in_group_periods(0)
{
	bloom_parameters params;
	params.false_positive_probability = 0.01;
	params.projected_element_count = group_ref.size;
	params.compute_optimal_parameters();
	current_filter = bloom_filter(params);
	filter2 = bloom_filter(params);
	filter3 = bloom_filter(params);
}

bloom_detector::group_data::group_data() :
		current_filter(), filter2(), filter3(),
		bloom_filter_hits(0),
		interval_hit_count(0),
				lower_group_id(0), upper_group_id(0), index(0), groups_none(), groups(groups_none), age_in_intervals(0)
{
}

void adaptive_bloom_detector::update_probilities(double current_time) {
	// Update probabilities of groups

	if (data.size() == 0) {
		return;
	}

	current_interval_counter = get_interval_length();
	for (int i = 0; i < data.size(); i++) {
		double length = get_interval_length();
		double new_prob = data[i]->interval_hit_count / length;
		groups[i].actual_prob = groups[i].actual_prob * (1.0 - interval_size_of_the_lba_space * 3) + new_prob * interval_size_of_the_lba_space * 3;
		data[i]->interval_hit_count = 0;
		data[i]->age_in_intervals++;
	}

	vector<group_data*> copy = data;
	sort(copy.begin(), copy.end(), [](group_data* a, group_data* b) { return a->get_hits_per_page() < b->get_hits_per_page(); });

	for (auto c : copy) {
		if (copy.size() > 1 && c->age_in_intervals < 6) {
			return;
		}
	}

	for (int i = 1; i < copy.size() - 1; i++) {
		if (copy[i]->get_hits_per_page() * 100 < copy[i-1]->get_hits_per_page() * 100 * 1.2 &&
				copy[i]->get_hits_per_page() * 100 * 1.2 > copy[i+1]->get_hits_per_page() * 100 &&
				copy[i]->age_in_group_periods >= 3 &&
				group::is_stable()) {
			printf("%f   %f    %f  destroy group %d, others are %d and %d\n",
					copy[i-1]->get_hits_per_page() * 100,
					copy[i]->get_hits_per_page() * 100,
					copy[i+1]->get_hits_per_page() * 100,
					copy[i]->index,
					copy[i-1]->index,
					copy[i+1]->index);
			printf("condition1:  %d\n", copy[i]->get_hits_per_page() * 100 < copy[i-1]->get_hits_per_page() * 100 * 1.2);
			printf("condition2:  %d\n", copy[i]->get_hits_per_page() * 100 * 1.2 > copy[i+1]->get_hits_per_page() * 100);
			printf("condition3:  %d\n", copy[i]->age_in_group_periods > 1.0 / interval_size_of_the_lba_space);
			printf("condition4:  %d\n", group::is_stable());

			merge_groups(copy[i-1], copy[i], current_time);
			copy.erase(copy.begin() + i);

			break;
		}
	}

	int lowest_group_id = copy[0]->index;
	data[lowest_group_id]->lower_group_id = UNDEFINED;
	lowest_group = data[lowest_group_id];

	int highest_group_id = copy[copy.size() - 1]->index;
	data[highest_group_id]->upper_group_id = UNDEFINED;
	highest_group = data[highest_group_id];
	for (int i = 0; i < copy.size(); i++) {
		int group_id = copy[i]->index;
		if (i + 1 < copy.size()) {
			//copy[i].upper_group_id = copy[i + i].group_ref.id;
			data[group_id]->upper_group_id = copy[i + 1]->index;
		}
		if (i - 1 >= 0) {
			//copy[i]->upper_group_id = copy[i - i]->group_ref.id;
			data[group_id]->lower_group_id = copy[i - 1]->index;
		}
	}
	// check how close some groups are now to each other, and merge or create new groups as needed
}

non_adaptive_bloom_detector::non_adaptive_bloom_detector(vector<group>& groups, Block_Manager_Groups* bm) :
		bloom_detector(groups, bm), hit_rate()
{}

void non_adaptive_bloom_detector::change_in_groups(vector<group>& groups, double current_time) {
	for (int i = 0; i < groups.size(); i++) {
		hit_rate.push_back(pow(2, i));
	}
	bloom_detector::change_in_groups(groups, current_time);
	for (int i = 0; i < groups.size(); i++) {
		data[i]->upper_group_id = i + 1;
		data[i]->lower_group_id = i - 1;
	}
	highest_group = data.back();
	lowest_group = data.front();
	data.back()->upper_group_id = UNDEFINED;
	data.front()->lower_group_id = UNDEFINED;
}

void non_adaptive_bloom_detector::update_probilities(double current_time) {
	// Update probabilities of groups
	if (data.size() == 0) {
		return;
	}

	current_interval_counter = get_interval_length();
	// update the hit rates for any new groups
	double denominator = 0;
	int num_pages = 0;

	while (hit_rate.size() != groups.size()) {
		hit_rate.push_back(0);
	}

	for (int i = 0; i < data.size(); i++) {
		num_pages += groups[i].num_pages;
		double length = get_interval_length();
		if (hit_rate[i] == 0 && data[i]->lower_group_id != UNDEFINED) {
			hit_rate[i] = hit_rate[data[i]->lower_group_id] * 2;
		}
		else if (hit_rate[i] == 0 && data[i]->upper_group_id != UNDEFINED) {
			hit_rate[i] = hit_rate[data[i]->upper_group_id] / 2;
		}
		denominator += hit_rate[i] * groups[i].num_pages;
	}

	if (num_pages == 0) {
		return;
	}

	for (int i = 0; i < data.size(); i++) {
		double length = get_interval_length();
		groups[i].actual_prob = (groups[i].num_pages * hit_rate[i]) / denominator;
		data[i]->interval_hit_count = 0;
		data[i]->age_in_intervals++;
	}

}
