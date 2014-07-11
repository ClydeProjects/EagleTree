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
	else {
		detector = new bloom_detector(groups, this);
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
	group::mapping_pages_to_groups =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
	init_detector();
	group new_group(1, NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR, this, ssd, 0);
	groups.push_back(new_group);
	detector->change_in_groups(groups, 0);
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
	if (new_groups.size() == 0) {
		return;
	}
	groups.clear();
	int offset = 0;
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
		if (la >= groups[i].offset && la < groups[i].offset + groups[i].size) {
			return i;
		}
	}
	return UNDEFINED;
}

void Block_Manager_Groups::register_write_outcome(Event const& event, enum status status) {
	int package = event.get_address().package;
	int die = event.get_address().die;

	Block_manager_parent::register_write_outcome(event, status);

	int la = event.get_logical_address();
	int prior_group_id = group::mapping_pages_to_groups.at(la);
	int ideal_group_id = detector->which_group_should_this_page_belong_to(event);

	if (ideal_group_id < 0 || ideal_group_id >= groups.size()) {
		printf("group_id  %d\n", ideal_group_id);
		event.print();
	}
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

	if (groups[ideal_group_id].num_pages > groups[ideal_group_id].size * 1.02 || groups[ideal_group_id].actual_prob > groups[ideal_group_id].prob * 1.02) {
		printf("regrouping due to change!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
			groups[i].prob = groups[i].actual_prob;
		}
		groups = group::iterate(groups);
		print();
		//group::init_stats(groups);
		//StatisticsGatherer::get_global_instance()->print();
		//StatisticsGatherer::init();
	}

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
		groups = group::iterate(groups);
		print();

		double factor_g1, factor_g2;
		if (groups[0].stats.num_gc_in_group == 0 || groups[1].stats.num_gc_in_group == 0) {
			factor_g1 = factor_g2 = 0;
		} else {
			factor_g1 = groups[0].stats.num_gc_writes_to_group / groups[0].stats.num_gc_in_group;
			factor_g2 = groups[1].stats.num_gc_writes_to_group / groups[1].stats.num_gc_in_group;
		}

		StatisticData::register_statistic("groups_gc", {
				new Integer(count),
				new Integer(factor_g1),
				new Integer(groups[0].stats.num_gc_writes_to_group),
				new Integer(factor_g2),
				new Integer(groups[1].stats.num_gc_writes_to_group)
		});

		StatisticData::register_field_names("groups_gc", {
				"num_writes",
				"group 1 factor",
				"group 1 gc",
				"group 2 factor",
				"group 2 gc"
		});

		group::init_stats(groups);
		stats.num_group_misses = 0;
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
	bool enough_free_blocks = try_to_allocate_block_to_group(group_id, package, die, event.get_current_time());
	if (!enough_free_blocks) {
		request_gc(group_id, package, die, event.get_current_time());
	}
}

bool Block_Manager_Groups::may_garbage_collect_this_block(Block* block, double current_time) {
	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			group_id = i;
		}
	}

	static int p0 = 0, p1 = 0;
	Address addr = Address(block->get_physical_address(), BLOCK);
	if (addr.package == 0) {
		p0++;
	} else {
		p1++;
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

	if (addr.package == 1) {
		int i = 0;
		i++;
	}

	if (!equib && !need_more_blocks && num_active_blocks + num_blocks_being_gc >= SSD_SIZE * PACKAGE_SIZE ) {
		//print();
		return false;
	}

	groups[group_id].blocks_being_garbage_collected.insert(block);
	assert(groups[group_id].blocks_being_garbage_collected.size() <= SSD_SIZE * PACKAGE_SIZE);

	//printf("num free blocks in SSD  %d   and on same LUN %d   %d     requests to p0: %d  and to p1: %d\n",
	//		get_num_free_blocks(), get_num_free_blocks(0, 0), get_num_free_blocks(1, 0), p0, p1);

	/*if (get_num_free_blocks() > SSD_SIZE * PACKAGE_SIZE) {
		int i = 0;
		i++;
	}*/

	//printf("valid:  %d  \t group:   %d\t", block->get_pages_valid(), group_id);

	//printf("\n");


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

bool Block_Manager_Groups::try_to_allocate_block_to_group(int group_id, int package, int die, double time) {
	bool starved = groups[group_id].is_starved();

	bool needs_more_blocks = groups[group_id].needs_more_blocks();
	int actual_size = groups[group_id].block_ids.size() * BLOCK_SIZE;
	int OP = groups[group_id].OP;
	int size = groups[group_id].size;
	int num_free_blocks = get_num_free_blocks(package, die);

	if (needs_more_blocks || starved || num_free_blocks > 1) {
		if (!has_free_pages(groups[group_id].free_blocks.blocks[package][die])) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
				groups[group_id].num_blocks_per_die[package][die]++;
			}
		}
		if (!has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && !starved) {
			Address block_addr = find_free_unused_block(package, die, time);
			if (has_free_pages(block_addr)) {
				groups[group_id].next_free_blocks.blocks[package][die] = block_addr;
				Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
				assert(groups[group_id].block_ids.count(block) == 0);
				groups[group_id].block_ids.insert(block);
				groups[group_id].num_blocks_per_die[package][die]++;
			}
		}
	}
	//printf("gc in group since it is starved. group %d \n", group_id);

	/*if (num_free_blocks > 10) {
		return true;
	}*/

	if (groups[group_id].num_blocks_per_die[package][die] * 1.15 < groups[group_id].get_avg_blocks_per_die()) {
		trigger_gc_in_same_lun_but_different_group(package, die, group_id, time);
		return true;
	}

	//if (needs_more_blocks || groups[group_id].in_equilbirium()) {
	if (needs_more_blocks || group::in_total_equilibrium(groups, group_id)) {

		//printf("group %d needs more blocks  %d\n", group_id, has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && has_free_pages(groups[group_id].free_blocks.blocks[package][die]));
		return has_free_pages(groups[group_id].next_free_blocks.blocks[package][die]) && has_free_pages(groups[group_id].free_blocks.blocks[package][die]);
	}
	bool still_starved = groups[group_id].is_starved();

	//printf("group %d starved\n", group_id);
	return !still_starved;
}

void Block_Manager_Groups::trigger_gc_in_same_lun_but_different_group(int package, int die, int group_id, double time) {

	if (get_num_free_blocks(package, die) > 1) {
		return;
	}

	double avg = groups[group_id].get_avg_blocks_per_die();
	int max_num_pages = 0;
	int selected_group = UNDEFINED;
	// identify a group with too many pages
	for (int i = 0; i < groups.size(); i++ ){
		if (group_id != i && groups[i].num_blocks_per_die[package][die] > max_num_pages) {
			max_num_pages = groups[i].num_blocks_per_die[package][die];
			selected_group = i;
		}
	}

	if (groups[selected_group].num_blocks_per_die[package][die] <= avg) {
		return;
	}

	// trigger gc in selected group

	request_gc(selected_group, package, die, time);

}

void Block_Manager_Groups::register_erase_outcome(Event const& event, enum status status) {
	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	//PRINT_LEVEL = 1;
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
	if (rand() % 3 == 0) {
		group::count_num_groups_that_need_more_blocks(groups);
	}
}

void Block_Manager_Groups::check_if_should_trigger_more_GC(Event const& event) {

	Address a = event.get_address();
	int min_num_blocks = NUMBER_OF_ADDRESSABLE_BLOCKS();
	int selected_group = UNDEFINED;
	// identify a group with too many pages
	for (int i = 0; i < groups.size(); i++ ){
		if (groups[i].num_blocks_per_die[a.package][a.die] < min_num_blocks) {
			min_num_blocks = groups[i].num_blocks_per_die[a.package][a.die];
			selected_group = i;
		}
	}
	if (selected_group != UNDEFINED) {
		double avg = groups[selected_group].get_avg_blocks_per_die();
		if (min_num_blocks * 1.05 < avg) {
			int curr_num_blocks = groups[selected_group].block_ids.size();
			try_to_allocate_block_to_group(selected_group, a.package, a.die, event.get_current_time());
			if (groups[selected_group].block_ids.size() > curr_num_blocks) {
				//printf("successfully donated block! \n");
			}
		}
	}


	vector<int> order_group = Random_Order_Iterator::get_iterator(groups.size());
 	for (auto g : order_group) {
 		vector<int> order_packages = Random_Order_Iterator::get_iterator(SSD_SIZE);
		for (auto p : order_packages) {
			vector<int> order_dies = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
			for (auto d : order_dies) {
				bool enough_free_blocks = try_to_allocate_block_to_group(g, p, d, event.get_current_time());
				if (!enough_free_blocks) {
					request_gc(g, p, d, event.get_current_time());
				}
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
		int group_id = group::mapping_pages_to_groups.at(write.get_logical_address());
		write.set_tag(group_id);
	}
	int group_id = detector->which_group_should_this_page_belong_to(write);
	if ( groups.size() <= group_id) {
		print();
	}
	assert(groups.size() > 0);
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

void Block_Manager_Groups::add_group(double starting_prob_val) {
	group new_group = group(starting_prob_val, BLOCK_SIZE, this, ssd, groups.size());
	groups.push_back(new_group);
}

void Block_Manager_Groups::print() {
	printf(".........................................\n");
	group::print(groups);
	printf("num group misses: %d\n", stats.num_group_misses);
	printf("num free blocks in SSD:  %d\n", get_num_free_blocks());
	printf("num gc scheduled  %d\n", migrator->how_many_gc_operations_are_scheduled());
	printf("\n");
}

bloom_detector::bloom_detector(vector<group>& groups, Block_Manager_Groups* bm) :
	temperature_detector(groups), data(), bm(bm), current_interval_counter(get_interval_length()),
	interval_size_of_the_lba_space(0.002), lowest_group(NULL), highest_group(NULL)
{
	for (int i = 0; i < groups.size(); i++) {
		group_data* gd = new group_data(groups[i], groups);
		data.push_back(gd);
	}
	update_probilities(0);
}


void bloom_detector::change_in_groups(vector<group> const& groups, double current_time) {
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
		return lowest_group->id;
	}

	int num_occurances_in_filters = 0;
	num_occurances_in_filters += data[group_id]->current_filter.contains(la);
	num_occurances_in_filters += data[group_id]->filter2.contains(la);
	num_occurances_in_filters += data[group_id]->filter3.contains(la);

	// create new groups if needed
	if (false && num_occurances_in_filters == 0 && data[group_id]->lower_group_id == UNDEFINED && groups[group_id].num_pages > 10 * BLOCK_SIZE && !event.is_original_application_io()) {
		group_data* next_coldest = data[lowest_group->upper_group_id];
		if (data.size() == 1 || (lowest_group->get_hits_per_page() < next_coldest->get_hits_per_page() * 0.5 && lowest_group->get_group().size > next_coldest->get_group().size)) {
			printf("creating new cold group. coldest is: %d with %f and next is %d with %f\n", lowest_group->id, lowest_group->get_hits_per_page(), next_coldest->id, next_coldest->get_hits_per_page());
			groups[lowest_group->id].print();
			printf("update prob:    %f  size:    %f\n", lowest_group->update_probability, lowest_group->get_group().size);
			groups[next_coldest->id].print();
			printf("update prob:    %f  size:    %f\n", next_coldest->update_probability, next_coldest->get_group().size);
			bm->add_group(0);
			group const& new_group = groups.back();
			group_data* gd = new group_data(new_group, groups);
			data.push_back(gd);
			data[group_id]->lower_group_id = data.size() - 1;
			gd->upper_group_id = group_id;
			gd->lower_group_id = UNDEFINED;
		}
	}
	else if (data.size() < 4 && num_occurances_in_filters == 3 && data[group_id]->upper_group_id == UNDEFINED && groups[group_id].num_pages > 10 * BLOCK_SIZE && event.is_original_application_io()) {
		group_data* next_hottest = data[highest_group->lower_group_id];
		if (data.size() == 1 || highest_group->get_hits_per_page() * 0.5 > next_hottest->get_hits_per_page()) {
			if (data.size() > 1) {
				printf("creating new hot group. hottest is: %d with %f and next is %d with %f\n", highest_group->id, highest_group->get_hits_per_page(), next_hottest->id, next_hottest->get_hits_per_page());
			}
			bm->add_group(0);
			group const& new_group = groups.back();
			group_data* gd = new group_data(new_group, groups);
			data.push_back(gd);
			data[group_id]->upper_group_id = data.size() - 1;
			gd->lower_group_id = group_id;
			gd->upper_group_id = UNDEFINED;
		}
	}
	if (num_occurances_in_filters == 0 && data[group_id]->lower_group_id != UNDEFINED && !event.is_original_application_io()) {
		if (groups.size() <= group_id) {
			event.print();
			assert(false);
		}
		return data[group_id]->lower_group_id;
	}
	else if (num_occurances_in_filters == 3 && data[group_id]->upper_group_id != UNDEFINED && event.is_original_application_io()) {
		if (groups.size() <= group_id) {
			event.print();
			assert(false);
		}
		return data[group_id]->upper_group_id;
	}
	if (groups.size() <= group_id) {
		event.print();
		assert(false);
	}
	return group_id;
}

void bloom_detector::register_write_completed(Event const& event, int prior_group_id, int new_group_id) {

	if (!event.is_original_application_io()) {
		return;
	}
	if (event.get_logical_address() == 599111) {
		printf("599111 to group %d\n", new_group_id);
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
		bloom_detector::update_probilities(event.get_current_time());
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
}

void bloom_detector::change_id_for_pages(int old_id, int new_id) {
	for (int i = 0; i < group::mapping_pages_to_groups.size(); i++) {
		/*if (i == 404531) {
			printf("404531  bef %d\n", group::mapping_pages_to_groups[i], group::mapping_pages_to_groups[i] == old_id);
		}*/
		if (group::mapping_pages_to_groups[i] == old_id) {
			group::mapping_pages_to_groups[i] = new_id;
		}
		/*if (i == 404531) {
			printf("404531   aft  %d\n", group::mapping_pages_to_groups[i]);
		}*/
	}
}

void bloom_detector::merge_groups(group_data* gd1, group_data* gd2, double current_time) {
	group& g1 = groups[gd1->id];
	group& g2 = groups[gd2->id];
	group::print(groups);
	printf("age:  %d\n ", gd1->age_in_intervals);
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



	change_id_for_pages(g2.id, g1.id);

	// erase the group
	groups.erase(groups.begin() + gd2->id);
	delete data[gd2->id];
	data.erase(data.begin() + gd2->id);

	// update the ids of the groups to match the indicies
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].id != i) {
			printf("changing %d to %d\n", groups[i].id, i);
			change_id_for_pages(groups[i].id, i);
			groups[i].id = i;
			data[i]->id = i;
		}
	}



	group::iterate(groups);
}


void bloom_detector::update_probilities(double current_time) {
	// Update probabilities of groups

	if (data.size() == 0) {
		return;
	}

	current_interval_counter = get_interval_length();
	for (int i = 0; i < data.size(); i++) {
		double length = get_interval_length();
		double new_prob = data[i]->interval_hit_count / length;
		data[i]->update_probability = data[i]->update_probability * (1.0 - interval_size_of_the_lba_space * 3) + new_prob * interval_size_of_the_lba_space * 3;
		groups[i].actual_prob = data[i]->update_probability;
		data[i]->interval_hit_count = 0;
		data[i]->age_in_intervals++;
	}

	vector<group_data*> copy = data;
	sort(copy.begin(), copy.end(), [](group_data* a, group_data* b) { return a->get_hits_per_page() < b->get_hits_per_page(); });

	for (int i = 1; i < copy.size() - 1; i++) {
		if (copy[i]->get_hits_per_page() < copy[i-1]->get_hits_per_page() * 1.5 &&
				copy[i]->get_hits_per_page() * 1.5 > copy[i+1]->get_hits_per_page() &&
				copy[i]->age_in_intervals > 1.0 / interval_size_of_the_lba_space) {
			printf("%f   %f    %f  destroy group %d, others are %d and %d\n",
					copy[i-1]->get_hits_per_page(),
					copy[i]->get_hits_per_page(),
					copy[i+1]->get_hits_per_page(),
					copy[i]->id,
					copy[i-1]->id,
					copy[i+1]->id);
			merge_groups(copy[i-1], copy[i], current_time);
			copy.erase(copy.begin() + i);

			break;
		}
	}

	int lowest_group_id = copy[0]->id;
	data[lowest_group_id]->lower_group_id = UNDEFINED;
	lowest_group = data[lowest_group_id];

	int highest_group_id = copy[copy.size() - 1]->id;
	data[highest_group_id]->upper_group_id = UNDEFINED;
	highest_group = data[highest_group_id];
	for (int i = 0; i < copy.size(); i++) {
		int group_id = copy[i]->id;
		if (i + 1 < copy.size()) {
			//copy[i].upper_group_id = copy[i + i].group_ref.id;
			data[group_id]->upper_group_id = copy[i + 1]->id;
		}
		if (i - 1 >= 0) {
			//copy[i]->upper_group_id = copy[i - i]->group_ref.id;
			data[group_id]->lower_group_id = copy[i - 1]->id;
		}
	}
	// check how close some groups are now to each other, and merge or create new groups as needed
}

bloom_detector::group_data::group_data(group const& group_ref, vector<group> const& groups) :
		current_filter(), filter2(), filter3(),
		bloom_filter_hits(group_ref.size),
		update_probability(group_ref.prob), interval_hit_count(0),
				lower_group_id(0), upper_group_id(0), id(group_ref.id), groups(groups), age_in_intervals(0)
{
	bloom_parameters params;
	params.false_positive_probability = 0.01;
	params.projected_element_count = group_ref.size;
	params.compute_optimal_parameters();
	current_filter = bloom_filter(params);
	filter2 = bloom_filter(params);
	filter3 = bloom_filter(params);
}

