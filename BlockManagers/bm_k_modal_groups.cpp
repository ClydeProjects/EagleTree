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
bool Block_Manager_Groups::prioritize_groups_that_need_blocks = 0;
int Block_Manager_Groups::garbage_collection_policy_within_groups = 0;

int bloom_detector::num_filters = 3;
int bloom_detector::max_num_groups = 20;
int bloom_detector::min_num_groups = 5;
double bloom_detector::bloom_false_positive_probability = 0.1;

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
	else if (detector_type == 2) {
		detector = new non_adaptive_bloom_detector(groups, this);
	}
	else {
		detector = new tag_based_with_prob_recomp(groups, this);
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
	//group::mapping_pages_to_groups =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
	//group::mapping_pages_to_tags =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1, UNDEFINED);
	group::mapping_pages_to_groups =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() + 1, UNDEFINED);
	group::mapping_pages_to_tags =  vector<int>(NUMBER_OF_ADDRESSABLE_PAGES() + 1, UNDEFINED);
	if (groups.size() == 0) {
		groups.push_back(group(1, NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR, this, ssd, 0));
	}
	init_detector();
	detector->change_in_groups(groups, 0);
	groups = group::allocate_op(groups);

}

void Block_Manager_Groups::change_update_frequencies(Groups_Message const& msg) {
	double total_prob = 0;
	for (int i = 0; i < msg.groups.size(); i++) {
		total_prob += msg.groups[i].update_frequency;
	}
	for (int i = 0; i < msg.groups.size(); i++) {
		groups[i].prob = groups[i].actual_prob = msg.groups[i].update_frequency / total_prob;
	}
	vector<group> opt_groups = group::allocate_op(groups);
	groups = opt_groups;
	//group::init_stats(groups);
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

	double total_prob = 0;
	for (int i = 0; i < new_groups.size(); i++) {
		total_prob += new_groups[i].update_frequency;
	}

	for (int i = 0; i < new_groups.size(); i++) {
		double update_prob = new_groups[i].update_frequency / total_prob;
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
	vector<group> opt_groups = group::allocate_op(groups);
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

void Block_Manager_Groups::register_write_arrival(Event const& e) {
	/*printf("groups.size(): %d\n", groups.size());
	if (e.get_tag() == groups.size()) {
		group g(0, 1, this, ssd, groups.size());
		groups.push_back(g);
		vector<group> opt_groups = group::allocate_op(groups);
		group::print(opt_groups);
		groups = opt_groups;
	}*/
}

int tag_detector::which_group_should_this_page_belong_to(Event const& event) {
	if (event.get_tag() != UNDEFINED) {
		return event.get_tag();
	}
	int la = event.get_logical_address();
	int smallest_non_matching_group = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (la >= groups[i].offset && la < groups[i].offset + groups[i].size + i) {
			return i;
		}
		if (la >= groups[i].offset) {
			smallest_non_matching_group = i;
		}
	}
	//printf("no group for event ");
	//event.print();
	return smallest_non_matching_group;
}

void Block_Manager_Groups::register_write_outcome(Event const& event, enum status status) {
	int package = event.get_address().package;
	int die = event.get_address().die;

	Block_manager_parent::register_write_outcome(event, status);

	int la = event.get_logical_address();
	int prior_group_id = group::mapping_pages_to_groups.at(la);
	int ideal_group_id = detector->which_group_should_this_page_belong_to(event);
	if (ideal_group_id == 1) {
		int i = 0;
		i++;
	}
	if (event.get_logical_address() == 356358) {
		//event.print();
		int i = 0;
		i++;
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

	groups[ideal_group_id].register_write_outcome(event);

	if (event.is_original_application_io()) {
		groups[ideal_group_id].stats.num_writes_to_group++;
	}
	else if (event.is_mapping_op() && !event.is_garbage_collection_op()) {
		groups[ideal_group_id].stats.num_writes_to_group++;
	}
	else if (event.is_garbage_collection_op()) {
		assert(prior_group_id != UNDEFINED);
		groups[prior_group_id].stats.num_gc_writes_to_group++;
	}

	detector->register_write_completed(event, prior_group_id, ideal_group_id);

	if (groups[ideal_group_id].num_pages > groups[ideal_group_id].size * 1.05 || groups[ideal_group_id].actual_prob > groups[ideal_group_id].prob * 1.05) {
		//printf("regrouping due to change!!!\n");
		for (int i = 0; i < groups.size(); i++) {
			groups[i].size = groups[i].num_pages;
			groups[i].prob = groups[i].actual_prob;
		}
		groups = group::allocate_op(groups);


		group::num_writes_since_last_regrouping = 0;
		//print();
		//group::init_stats(groups);
		//StatisticsGatherer::get_global_instance()->print();
		//StatisticsGatherer::init();
	}
	group::num_writes_since_last_regrouping++;

	static int count = 0;
	int lba = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	if (event.is_original_application_io()) {
		count++;
	}

	if (/*event.get_id() > lba && event.is_original_application_io() && */count % 50000 == 0) {
		count++;
		printf("regrouping!!!\n");
		if (event.get_id() > lba && event.is_original_application_io()  ) {
			for (int i = 0; i < groups.size(); i++) {
				groups[i].size = groups[i].num_pages;
				groups[i].prob = groups[i].actual_prob;
			}

		}
		groups = group::allocate_op(groups);
		print();

		printf("num writes:  %d   %d\n", StatisticsGatherer::get_global_instance()->total_writes(), count);

		double factor_g1 = groups[0].stats.num_gc_in_group == 0 ? 0 : (double)groups[0].stats.num_gc_writes_to_group / groups[0].stats.num_gc_in_group;
		double factor_g2 = groups[1].stats.num_gc_in_group == 0 ? 0 : (double)groups[1].stats.num_gc_writes_to_group / groups[1].stats.num_gc_in_group;
		int sum_gc = 0;
		for (auto g : groups) {
			sum_gc += g.stats.num_gc_writes_to_group;
		}

		if (factor_g2 > BLOCK_SIZE) {
			//printf("%f    %d  /   %d", factor_g1, groups[1].stats.num_gc_writes_to_group, groups[1].stats.num_gc_in_group);
			//exit(1);
		}

		/*if (groups[0].stats.num_gc_writes_to_group > 10000) {
			groups[0].print();
			event.print();
		}*/

		StatisticData::register_statistic("groups_gc", {
				new Integer(count),
				new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
				new Integer(groups[0].stats.num_gc_writes_to_group),
				new Integer(groups[0].stats.num_writes_to_group),
				new Integer(groups[0].stats.num_requested_gc),
				new Integer(groups[0].stats.num_requested_gc_to_balance),
				new Integer(groups[0].num_pages),
				new Integer(groups[0].prob * 100),
				new Double(groups[0].get_normalized_hits_per_page()),
				new Integer(groups[0].OP),

				new Integer(groups.size() < 2 ? 0 : groups[1].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 2 ? 0 : groups[1].stats.num_writes_to_group),
				new Integer(groups.size() < 2 ? 0 : groups[1].stats.num_requested_gc),
				new Integer(groups.size() < 2 ? 0 : groups[1].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 2 ? 0 : groups[1].num_pages),
				new Integer(groups.size() < 2 ? 0 : groups[1].prob * 100),
				new Double(groups.size() < 2 ? 0 : groups[1].get_normalized_hits_per_page()),
				new Integer(groups.size() < 2 ? 0 : groups[1].OP),

				new Integer(groups.size() < 3 ? 0 : groups[2].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 3 ? 0 : groups[2].stats.num_writes_to_group),
				new Integer(groups.size() < 3 ? 0 : groups[2].stats.num_requested_gc),
				new Integer(groups.size() < 3 ? 0 : groups[2].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 3 ? 0 : groups[2].num_pages),
				new Integer(groups.size() < 3 ? 0 : groups[2].prob * 100),
				new Double(groups.size() < 3 ? 0 : groups[2].get_normalized_hits_per_page()),
				new Integer(groups.size() < 3 ? 0 : groups[2].OP),

				new Integer(groups.size() < 4 ? 0 : groups[3].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 4 ? 0 : groups[3].stats.num_writes_to_group),
				new Integer(groups.size() < 4 ? 0 : groups[3].stats.num_requested_gc),
				new Integer(groups.size() < 4 ? 0 : groups[3].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 4 ? 0 : groups[3].num_pages),
				new Integer(groups.size() < 4 ? 0 : groups[3].prob * 100),
				new Double(groups.size() < 4 ? 0 : groups[3].get_normalized_hits_per_page()),
				new Integer(groups.size() < 4 ? 0 : groups[3].OP),

				new Integer(groups.size() < 5 ? 0 : groups[4].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 5 ? 0 : groups[4].stats.num_writes_to_group),
				new Integer(groups.size() < 5 ? 0 : groups[4].stats.num_requested_gc),
				new Integer(groups.size() < 5 ? 0 : groups[4].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 5 ? 0 : groups[4].num_pages),
				new Integer(groups.size() < 5 ? 0 : groups[4].prob * 100),
				new Double(groups.size() < 5 ? 0 : groups[4].get_normalized_hits_per_page()),
				new Integer(groups.size() < 5 ? 0 : groups[4].OP),

				new Integer(groups.size() < 6 ? 0 : groups[5].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 6 ? 0 : groups[5].stats.num_writes_to_group),
				new Integer(groups.size() < 6 ? 0 : groups[5].stats.num_requested_gc),
				new Integer(groups.size() < 6 ? 0 : groups[5].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 6 ? 0 : groups[5].num_pages),
				new Integer(groups.size() < 6 ? 0 : groups[5].prob * 100),
				new Double(groups.size() < 6 ? 0 : groups[5].get_normalized_hits_per_page()),
				new Integer(groups.size() < 6 ? 0 : groups[5].OP),

				new Integer(groups.size() < 7 ? 0 : groups[6].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 7 ? 0 : groups[6].stats.num_writes_to_group),
				new Integer(groups.size() < 7 ? 0 : groups[6].stats.num_requested_gc),
				new Integer(groups.size() < 7 ? 0 : groups[6].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 7 ? 0 : groups[6].num_pages),
				new Integer(groups.size() < 7 ? 0 : groups[6].prob * 100),
				new Double(groups.size() < 7 ? 0 : groups[6].get_normalized_hits_per_page()),
				new Integer(groups.size() < 7 ? 0 : groups[6].OP),

				new Integer(groups.size() < 8 ? 0 : groups[7].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 8 ? 0 : groups[7].stats.num_writes_to_group),
				new Integer(groups.size() < 8 ? 0 : groups[7].stats.num_requested_gc),
				new Integer(groups.size() < 8 ? 0 : groups[7].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 8 ? 0 : groups[7].num_pages),
				new Integer(groups.size() < 8 ? 0 : groups[7].prob * 100),
				new Double(groups.size() < 8 ? 0 : groups[7].get_normalized_hits_per_page()),
				new Integer(groups.size() < 8 ? 0 : groups[7].OP),

				new Integer(groups.size() < 9 ? 0 : groups[8].stats.num_gc_writes_to_group),
				new Integer(groups.size() < 9 ? 0 : groups[8].stats.num_writes_to_group),
				new Integer(groups.size() < 9 ? 0 : groups[8].stats.num_requested_gc),
				new Integer(groups.size() < 9 ? 0 : groups[8].stats.num_requested_gc_to_balance),
				new Integer(groups.size() < 9 ? 0 : groups[8].num_pages),
				new Integer(groups.size() < 9 ? 0 : groups[8].prob * 100),
				new Double(groups.size() < 9 ? 0 : groups[8].get_normalized_hits_per_page()),
				new Integer(groups.size() < 9 ? 0 : groups[8].OP),
				new Integer(sum_gc)
		});

		StatisticData::register_field_names("groups_gc", {
				"count",
				"writes",

				"gc1",
				"writes1",
				"gcReq1",
				"balance1",
				"size1",
				"prob1",
				"hits1",
				"OP1",

				"gc2",
				"writes2",
				"gcReq2",
				"balance2",
				"size2",
				"prob2",
				"hits2",
				"OP2",

				"gc3",
				"writes3",
				"gcReq3",
				"balance3",
				"size3",
				"prob3",
				"hits3",
				"OP3",

				"gc4",
				"writes4",
				"gcReq4",
				"balance4",
				"size4",
				"prob4",
				"hits4",
				"OP4",

				"gc5",
				"writes5",
				"gcReq5",
				"balance5",
				"size5",
				"prob5",
				"hits5",
				"OP5",

				"gc6",
				"writes6",
				"gcReq6",
				"balance6",
				"size6",
				"prob6",
				"hits6",
				"OP6",

				"gc7",
				"writes7",
				"gcReq7",
				"balance7",
				"size7",
				"prob7",
				"hits7",
				"OP7",

				"gc8",
				"writes8",
				"gcReq8",
				"balance8",
				"size8",
				"prob8",
				"hits8",
				"OP8",

				"gc9",
				"writes9",
				"gcReq9",
				"balance9",
				"size9",
				"prob9",
				"hits9",
				"OP9",

				"Total-GC"
		});

			StatisticData::register_statistic("blocks_per_group", {
					new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
					new Integer(groups[0].block_ids.size()),
					new Integer(groups[1].block_ids.size()),
			});

			StatisticData::register_field_names("blocks_per_group", {
					"num_writes",
					"group0-blocks",
					"group1-blocks",
			});


		  static clock_t time_sig = 0;
		  clock_t time_now = clock();
		  if (time_sig > 0) {
			  double elapsed_secs = double(time_now - time_sig) / CLOCKS_PER_SEC;
			  printf("elapsed_secs:   %f\n", elapsed_secs);
		  }
		time_sig = time_now;

		group::init_stats(groups);
		stats.num_group_misses = 0;
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

	if (current_time > 1185561) {
		int i = 0;
		i++;
	}

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

	//bool equib = group::in_total_equilibrium(groups, group_id);
	bool equib = groups[group_id].in_equilbirium();

	//int num_active_blocks = groups[group_id].free_blocks.get_num_free_blocks();
	//int num_blocks_being_gc = groups[group_id].blocks_being_garbage_collected.size();

	if (get_num_free_blocks(addr.package, addr.die) > 1) {
		//printf("triggering gc in %d %d even though there are %d free blocks \n", addr.package, addr.die, get_num_free_blocks(addr.package, addr.die));
		//return false;
	}

	// ENABLE when using the temperature detector. This is useful during group creation
	if (block->get_pages_valid() > groups[group_id].get_avg_pages_per_block_per_die()) {
		//return false;
	}

	// This lines seems to eliminate the spike in gc during group creation
	// however, it sometimes does cause us to pause indefinitely. Need to fix that.
	/*if (groups.size() > 1 && group_id == 1) {
		int i = 0;
		i++;
		//groups[1].print();
	}*/

	if (prioritize_groups_that_need_blocks && groups.size() > 1 && groups[group_id].num_app_writes < BLOCK_SIZE * 20 * 2 && NUMBER_OF_ADDRESSABLE_PAGES() * 2 < StatisticsGatherer::get_global_instance()->total_writes()) {
		return false;
	}

	if (prioritize_groups_that_need_blocks && !equib && need_more_blocks && get_num_pages_available_for_new_writes() >= BLOCK_SIZE) {
		/*if (StatisticsGatherer::get_global_instance()->total_writes() > 2334004) {
			//print();
			int pointers_group1 = groups[0].free_blocks.get_num_free_blocks();
			int pointers_group2 = groups[1].free_blocks.get_num_free_blocks();
			//printf("cancelling.  ongoing gc: %d   pointers1: %d   pointers2: %d   pages for new writes:  %d",
			//		ongoing_gc, pointers_group1, pointers_group2, get_num_available_pages_for_new_writes());
			f++;
			//printf("f\n", f);
		}*/
		return false;
	}

	if (block->get_pages_valid() > groups[group_id].size / groups[group_id].block_ids.size()) {
		//printf("working\n");
		//return false;
	}

	if (group_id == 0) {
		//printf("%d  %d  %d  %d \n", group_id, addr.package, addr.die, block->get_pages_valid());
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

	if (groups[group_id].free_blocks.get_num_free_blocks() == SSD_SIZE * PACKAGE_SIZE && groups[group_id].next_free_blocks.get_num_free_blocks() == SSD_SIZE * PACKAGE_SIZE) {
		//printf("full blocks\n");
		//return false;
	}

	// The usefulness of this return has not yet been established
	if (groups.size() > 1 && (groups[group_id].in_equilbirium() || groups[group_id].needs_more_blocks()) &&
			groups[group_id].free_blocks.get_num_free_blocks() == PACKAGE_SIZE * SSD_SIZE &&
			groups[group_id].next_free_blocks.get_num_free_blocks() == SSD_SIZE * PACKAGE_SIZE) {
		//printf("cancel\n");
		//return false;
	}

	/*if (group_id == 0) {
		printf("issue gc in 0:  %d\t", block->get_pages_valid());
		addr.print();
		printf("\tnum blocks: %d\tfree blocks: %d", groups[0].num_blocks_per_die[addr.package][addr.die], groups[0].free_blocks.get_num_free_blocks());
		printf("\treserve blocks: %d", groups[0].next_free_blocks.get_num_free_blocks());
		printf("\tequib: %d", groups[group_id].in_equilbirium());
		printf("\tneed blocks: %d", groups[group_id].needs_more_blocks());
		printf("\tnum blocks needed: %d\n", groups[group_id].needs_how_many_blocks());
	}*/



	groups[group_id].blocks_being_garbage_collected.insert(block);
	assert(groups[group_id].blocks_being_garbage_collected.size() <= SSD_SIZE * PACKAGE_SIZE);

	string name = "gc_for_group_" + to_string(group_id);
	StatisticData::register_statistic(name, {
			new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
			new Integer(group_id),
			new Integer(block->get_pages_valid())
	});

	StatisticData::register_field_names(name, {
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

	//if (prioritize_groups_that_need_blocks && !group::in_total_equilibrium(groups, group_id) && needs_more_blocks) {
	if (prioritize_groups_that_need_blocks && !groups[group_id].in_equilbirium() && needs_more_blocks) {
		bool triggered = trigger_gc_in_same_lun_but_different_group(package, die, group_id, time);
		/*if (triggered) {
			return;
		}*/
	}
	/*else if (group_id == 0 && !has_free_block) {
		stats.num_normal_gc_operations_requested++;
		request_gc(group_id, package, die, time);
	}*/
	else if ((!has_free_block || !has_reserve_block) && (needs_more_blocks || groups[group_id].in_equilbirium())) {
		stats.num_normal_gc_operations_requested++;
		request_gc(group_id, package, die, time);
	}
	else if (groups[group_id].is_starved()) {
		stats.num_starved_gc_operations_requested++;
		groups[group_id].stats.num_requested_gc_starved++;
		request_gc(group_id, package, die, time);
	}
}

bool Block_Manager_Groups::trigger_gc_in_same_lun_but_different_group(int package, int die, int group_id, double time) {
	if (get_num_free_blocks(package, die) > 1) {
		return false;
	}
	// find the group with the highest excess of blocks

	int max_excess_blocks_needed = 0;
	int selected_group = UNDEFINED;
	// identify a group with too many pages

	vector<int> order = Random_Order_Iterator::get_iterator(groups.size());
	for (auto i : order) {
		int num_excess_blocks = groups[i].block_ids.size() * BLOCK_SIZE - groups[i].OP - groups[i].size;
		if (num_excess_blocks > max_excess_blocks_needed) {
			selected_group = i;
			max_excess_blocks_needed = num_excess_blocks;
		}
 	}

	// ------------------------

	vector<int> order2 = Random_Order_Iterator::get_iterator(groups.size());
	int num_pages_to_migrate = BLOCK_SIZE;
	int best_index = UNDEFINED;
	for (auto i : order2) {
		if (!groups[i].in_equilbirium() && !groups[i].needs_more_blocks()) {
			Block* b = groups[i].get_gc_victim_greedy(package, die);
			if (b != NULL && b->get_pages_valid() < num_pages_to_migrate && b->get_pages_valid() < BLOCK_SIZE * 0.8) {
				num_pages_to_migrate = b->get_pages_valid();
				best_index = i;
			}
		}
	}
	if (best_index == UNDEFINED) {
		return false;
	}
	selected_group = best_index;
	// ------------------------

	/*if (group_id == selected_group) {
		print();
	}*/
	assert(group_id != selected_group);
	assert(group_id != UNDEFINED);
	if (!groups[selected_group].in_equilbirium() && !groups[selected_group].needs_more_blocks()) {
		//printf("need blocks in group %d. triggering gc in %d\n", group_id, selected_group);

		request_gc(selected_group, package, die, time);
		groups[selected_group].stats.num_requested_gc_to_balance++;
		return true;
	}
	return false;
}

void Block_Manager_Groups::register_erase_outcome(Event& event, enum status status) {
	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	int group_id = UNDEFINED;
	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.count(block) == 1) {
			//printf("block erased from group %d\n", i);
			groups[i].register_erase_outcome(event);
			group_id = i;
			event.set_tag(i);
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

	vector<int> num_blocks_per_group(groups.size(), 0);
	for (int i = 0; i < groups.size(); i++) {
		num_blocks_per_group[i] = groups[i].block_ids.size();
	}

	if (rand() % 2 == 0 && !groups[event.get_tag()].in_equilbirium() && groups[event.get_tag()].needs_more_blocks() ) {
		//give_block_to_group(event.get_address().package, event.get_address().die, event.get_tag(), event.get_current_time());
		//printf("returning block to group %d\n", event.get_tag());
	}

	vector<int> order = Random_Order_Iterator::get_iterator(groups.size());
	for (auto g : order) {

		//if (groups[g].id == 6 && !groups[6].in_equilbirium() && groups[6].needs_more_blocks()) {
		//	event.print();
		//}

		if (/*prioritize_groups_that_need_blocks &&*/ !groups[g].in_equilbirium() && groups[g].needs_more_blocks()) {
			int curr_num_blocks = groups[g].block_ids.size();

			try_to_allocate_block_to_group(g, event.get_address().package, event.get_address().die, event.get_current_time());

			if (groups[g].block_ids.size() > curr_num_blocks) {
				//printf("group %d. needs blocks:  %d   %d\n", g, groups[g].needs_more_blocks(), groups[g].needs_how_many_blocks());
				/*if (event.get_tag() == 6 && ) {
					printf("donation from %d to %d.");
				}*/
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

	for (int i = 0; i < groups.size(); i++) {
		if (groups[i].block_ids.size() > num_blocks_per_group[i] && event.get_tag() != i) {
			//assert(event.get_tag() == i);
			//if (!groups[6].in_equilbirium() && groups[6].needs_more_blocks() && event.get_tag() != i) {
			//printf("donating block from\t%d\tto\t%d\twhich needs %d blocks\n", event.get_tag(), i, groups[i].needs_how_many_blocks());
			//}
		}
	}

}

void Block_Manager_Groups::request_gc(int group_id, int package, int die, double time) {
	groups[group_id].stats.num_requested_gc++;
	Block* b = NULL;
	if (garbage_collection_policy_within_groups == 0) {
		b = groups[group_id].get_gc_victim_LRU(package, die);
	}
	else if (garbage_collection_policy_within_groups == 1) {
		b = groups[group_id].get_gc_victim_greedy(package, die);
	}
	else {
		b = groups[group_id].get_gc_victim_window_greedy(package, die);
	}
	if (b != NULL) {
		Address block_addr = Address(b->get_physical_address() , BLOCK);
		migrator->schedule_gc(time, package, die, block_addr.block, UNDEFINED);
	}
}

// handle garbage_collection case. Based on range.
Address Block_Manager_Groups::choose_best_address(Event& write) {
	if (write.get_id() == 2616381) {
		int i = 0;
		i++;
	}
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
		//int free1 = groups[0].free_blocks.get_num_free_blocks();
		//int free2 = groups[1].free_blocks.get_num_free_blocks();
		//printf("stuck in choose any addr. num writes  %d    free0: %d   free1: %d\n", StatisticsGatherer::get_global_instance()->total_writes(), free1, free2);
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

void Block_Manager_Groups::print() const {
	printf(".........................................\n");
	group::print_tags_distribution(groups);
	group::print(groups);
	double total_gc = 0;
	double total_writes = 0;
	for (auto g : groups) {
		total_gc += g.stats.num_gc_writes_to_group;
		total_writes += g.stats.num_writes_to_group;
	}
	double overall_write_amp = total_gc / total_writes;

	printf("num group misses: %d\n", stats.num_group_misses);
	printf("num normal gc operations requested: %d\n", stats.num_normal_gc_operations_requested);
	printf("num starved gc operations requested: %d\n", stats.num_starved_gc_operations_requested);
	printf("num free blocks in SSD:  %d\n", get_num_free_blocks());
	printf("num gc scheduled  %d\n", migrator->how_many_gc_operations_are_scheduled());
	printf("gc / writes = write amp:    %d / %d = %f\n", (int)total_gc, (int)total_writes, overall_write_amp);
	printf("\n");
}

bloom_detector::bloom_detector(vector<group>& groups, Block_Manager_Groups* bm) :
	temperature_detector(groups), data(), bm(bm), current_interval_counter(get_interval_length()),
	interval_size_of_the_lba_space(0.003), lowest_group(NULL), highest_group(NULL), tag_map(1000000, UNDEFINED)
{

	for (int i = groups.size(); i < min_num_groups; i++) {
		bm->add_group(0);
	}

	for (int i = 0; i < groups.size(); i++) {
		group_data* gd = new group_data(groups[i], groups);
		data.push_back(gd);
	}

	if (data.size() > 1) {
		data.front()->upper_group_id = data[1]->index;
		data.back()->lower_group_id = data[data.size() - 1]->index;
		lowest_group = data[0];
		highest_group = data.back();
		lowest_group = data.front();
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
	if (data.size() > 1) {
		data.front()->lower_group_id = UNDEFINED;
		data.front()->upper_group_id = 1;
		data.back()->lower_group_id = data.size() - 2;
		data.back()->upper_group_id = UNDEFINED;
		printf("%d  %d\n", data.front()->upper_group_id, data.back()->lower_group_id);
		highest_group = data.back();
		lowest_group = data.front();
	}
	for (int i = 1; i < data.size() - 1; i++) {
		data[i]->lower_group_id = data[i-1]->index;
		data[i]->upper_group_id = data[i+1]->index;
	}
	current_interval_counter = get_interval_length();
	update_probilities(current_time);
}

int bloom_detector::which_group_should_this_page_belong_to(Event const& event) {
	ulong la = event.get_logical_address();
	int group_id = group::mapping_pages_to_groups[la];

	if (tag_map[event.get_id() % tag_map.size()] != UNDEFINED) {
		return tag_map[event.get_id() % tag_map.size()];
	}
	// first time this logical addrwss is ever written. Write to the least updated group
	if (group_id == UNDEFINED || max_num_groups == 1) {
		return lowest_group->index;
	}
	// this early escape is in order to avoid searching the bloom filters, which seem to be a bottleneck
	if (event.is_original_application_io() && data[group_id] == highest_group && !create_higher_group(group_id)) {
		return highest_group->index;
	}

	int num_occurances_in_filters = data[group_id]->in_filters(event);
	if (num_occurances_in_filters >= 3) {
		//printf("num_occurances_in_filters %d\n", num_occurances_in_filters );
	}

	int size = data.size();

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
			data.size() < max_num_groups &&
			num_occurances_in_filters >= bloom_detector::num_filters && event.is_original_application_io() && create_higher_group(group_id)) {
		group_data* next_hottest = data[highest_group->lower_group_id];
		if (data.size() > 1) {
			printf("creating new hot group. hottest is: %d with %f and next is %d with %f\n", highest_group->index, highest_group->get_hits_per_page(), next_hottest->index, next_hottest->get_hits_per_page());
		}
		group::print(groups);
		printf("num writes so far:  %d\n", StatisticsGatherer::get_global_instance()->total_writes());
		printf("age_in_group_periods: %d\n", highest_group->age_in_group_periods);
		printf("age_in_intervals: %d\n", highest_group->age_in_group_periods);
		printf("writes: %d\n", highest_group->get_group().num_app_writes);
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

	//int group_id = UNDEFINED;
	if (num_occurances_in_filters == 0 && data[group_id]->lower_group_id != UNDEFINED && !event.is_original_application_io() && rand() % 8 == 0) {
		//printf("Demoting page with tag %d from %d to %d\n", event.get_tag(), group_id, data[group_id]->lower_group_id );
		group_id = data[group_id]->lower_group_id;
	}
	else if (num_occurances_in_filters == bloom_detector::num_filters && data[group_id]->upper_group_id != UNDEFINED && event.is_original_application_io()) {
		//printf("Promoting page with tag %d from %d to %d\n", event.get_tag(), group_id, data[group_id]->lower_group_id );
		group_id = data[group_id]->upper_group_id;
	}
	tag_map[event.get_id() % tag_map.size()] = group_id;
	return group_id;
}

// If a page is in all filters and is an application io, it is promoted.
// It if is in no filters and it is a gc io, it is demoted.
// In all other cases, the page stays in the same group.
// Since searching the bloom filters slows down performance, we try to avoid redundant searches.
int bloom_detector::group_data::in_filters(Event const& e) {
	int hits = 0, misses = 0;
	long key = e.get_logical_address();
	if (e.get_logical_address() == 599111) {
		int i = 0;
		i++;
	}
	for ( int i = filters.size() - 1; i >= 0; i--) {
		if (filters[i] != NULL && filters[i]->contains(key)) {
			hits++;
		}
		else {
			misses++;
		}
		if (misses > 0 && e.is_original_application_io()) {
			return hits;
		}
		if (hits > 0 && !e.is_original_application_io()) {
			return hits;
		}
	}
	return hits;
}

bool bloom_detector::create_higher_group(int i) const {
	group_data* next_hottest = data[highest_group->lower_group_id];
	bool enough_hit_rate_diff = data.size() == 1 || highest_group->get_hits_per_page() > next_hottest->get_hits_per_page() * 2;

	/*if (data.size() > 1) {
		enough_hit_rate_diff = true;
		group_data* current = highest_group;
		group_data* next_group = data[highest_group->lower_group_id];
		while (current->lower_group_id != UNDEFINED) {
			double hotter = current->get_hits_per_page();
			double colder = next_group->get_hits_per_page();
			if (hotter < colder * 2) {
				enough_hit_rate_diff = false;
				break;
			}
			current = next_group;
			next_group = data[current->lower_group_id];
		}
	}*/

	bool all = data[i]->upper_group_id == UNDEFINED &&
				groups[i].num_pages > 10 * BLOCK_SIZE &&
				data[i]->age_in_group_periods >= 2 &&
				data[i]->age_in_intervals > 100 &&
				data[i] == highest_group &&
				enough_hit_rate_diff &&
				groups[i].num_app_writes > BLOCK_SIZE * 1000;
	return all;
}

void bloom_detector::register_write_completed(Event const& event, int prior_group_id, int new_group_id) {

	tag_map[event.get_id() % tag_map.size()] = UNDEFINED;

	if (!event.is_original_application_io() && !event.is_mapping_op()) {
		return;
	}
	if (event.get_logical_address() == 599111) {
		//printf("599111 to group %d\n", new_group_id);
	}

	//assert(!data[group_id]->current_filter.contains(event.get_logical_address()));
	long key = event.get_logical_address();
	data[new_group_id]->filters[0]->insert(key);
	/*if (prior_group_id != new_group_id) {
		data[new_group_id]->filter2.insert(event.get_logical_address());
	}*/
	data[new_group_id]->interval_hit_count++;

	if (new_group_id == 1) {
		int i = 1;
		i++;
	}

	if (data[new_group_id]->bloom_filter_hits-- == 0) {
		group_interval_finished(new_group_id);
	}

	if (--current_interval_counter == 0) {
		update_probilities(event.get_current_time());
	}

}

void bloom_detector::group_interval_finished(int group_id) {
	//printf("group_interval_finished  %d\n", group_id);

	if (group_id == 1) {
		int i = 1;
		i++;
	}


	data[group_id]->bloom_filter_hits = groups[group_id].num_pages;
	if (data[group_id]->filters.back() != NULL) {
		delete data[group_id]->filters.back();
	}
	for (int i = bloom_detector::num_filters - 1; i >= 1; i--) {
		data[group_id]->filters[i] = data[group_id]->filters[i-1];
	}
	bloom_parameters params;
	params.false_positive_probability = bloom_false_positive_probability;
	params.projected_element_count = groups[group_id].num_pages;
	params.compute_optimal_parameters();
	data[group_id]->filters[0] = new bloom_filter(params);
	data[group_id]->age_in_group_periods++;
	//printf("group %d interval finished. num intervals %d\n", group_id, data[group_id]->age_in_group_periods);

	if (data[group_id]->age_in_group_periods == 11) {
		//long key =
		//assert(data[group_id]->filters[0]->contains(key));
	}

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
	group::allocate_op(groups);
}

bloom_detector::group_data::group_data(group const& group_ref, vector<group>& groups) :
		bloom_filter_hits(max(group_ref.size,10.0)),
		interval_hit_count(0), lower_group_id(0), upper_group_id(0), index(group_ref.index),
		groups_none(), groups(groups), age_in_intervals(0), age_in_group_periods(0), filters(bloom_detector::num_filters, NULL)
{
	bloom_parameters params;
	params.false_positive_probability = bloom_false_positive_probability;
	params.projected_element_count = group_ref.size;
	params.compute_optimal_parameters();
	filters[0] = new bloom_filter(params);
}

bloom_detector::group_data::group_data() :
		bloom_filter_hits(10),
		interval_hit_count(0),
		lower_group_id(0), upper_group_id(0), index(0), groups_none(), groups(groups_none), age_in_intervals(0),
		filters(bloom_detector::num_filters, NULL), age_in_group_periods(0)
{
}

void adaptive_bloom_detector::update_probilities(double current_time) {
	// Update probabilities of groups

	if (data.size() == 0) {
		return;
	}

	current_interval_counter = get_interval_length();
	double weight = interval_size_of_the_lba_space * 5;
	for (int i = 0; i < data.size(); i++) {
		double length = get_interval_length();
		double new_prob = data[i]->interval_hit_count / length;
		groups[i].actual_prob = groups[i].actual_prob * (1.0 - weight) + new_prob * weight;
		data[i]->interval_hit_count = 0;
		data[i]->age_in_intervals++;
	}

	adjust_groups(current_time);
}

void bloom_detector::try_to_merge_groups(double current_time) {
	vector<group_data*> copy = data;
	sort(copy.begin(), copy.end(), [](group_data* a, group_data* b) { return a->get_hits_per_page() < b->get_hits_per_page(); });

	for (int i = 1; i < copy.size() - 1; i++) {
		if (copy[i]->get_hits_per_page() * 100 < copy[i-1]->get_hits_per_page() * 100 * 1.3 &&
				copy[i]->get_hits_per_page() * 100 * 1.3 > copy[i+1]->get_hits_per_page() * 100 &&
				copy[i]->age_in_group_periods >= 3 &&
				group::is_stable() && groups.size() > min_num_groups) {

			printf("%f   %f    %f  merge groups %d, others are %d and %d\n",
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
}

void adaptive_bloom_detector::adjust_groups(double current_time) {

	for (int i = 0; i < data.size(); i++) {
		//printf("%d   %d\n", data[i]->get_group().num_app_writes, i);
		if (data.size() > 1 && (data[i]->age_in_intervals < 6 || data[i]->get_group().num_app_writes < BLOCK_SIZE * 100)) {
			return;
		}
	}
	//printf("%d   %d\n", data[0]->age_in_intervals, data[0]->age_in_group_periods);
	//printf("%d   %d\n", data[1]->age_in_intervals, data[1]->age_in_group_periods);
	//bm->print();

	try_to_merge_groups(current_time);

	vector<group_data*> copy = data;
	sort(copy.begin(), copy.end(), [](group_data* a, group_data* b) { return a->get_hits_per_page() < b->get_hits_per_page(); });

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
			int index = copy[i - 1]->index;
			data[group_id]->lower_group_id = index;
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

	for (int i = 1; i < data.size(); i++) {
		if (groups[i].num_pages < NUMBER_OF_ADDRESSABLE_PAGES() * 0.03 &&
				groups[i].num_app_writes > NUMBER_OF_ADDRESSABLE_PAGES() * 0.05 &&
				data[i]->age_in_group_periods >= 3 &&
				group::is_stable() && groups.size() > min_num_groups) {
			merge_groups(data[i], data[i+1], current_time);
			printf("%d   %d    merge groups %d and %d\n",
					groups[i+1].num_pages,
					groups[i].num_pages,
					data[i+1]->index,
					data[i]->index);
			break;
		}
	}

	hit_rate = vector<int>(groups.size());

	for (int i = 0; i < data.size(); i++) {
		num_pages += groups[i].num_pages;
		hit_rate[i] = pow(2, i);
		data[i]->lower_group_id = i - 1;
		data[i]->upper_group_id = i + 1;
		denominator += hit_rate[i] * groups[i].num_pages;
	}
	data.front()->lower_group_id = UNDEFINED;
	data.back()->upper_group_id = UNDEFINED;
	lowest_group = data.front();
	highest_group = data.back();

	if (num_pages == 0) {
		return;
	}

	for (int i = 0; i < data.size(); i++) {
		//double length = get_interval_length();
		//printf("%d\n", hit_rate[i]);
		groups[i].actual_prob = (groups[i].num_pages * hit_rate[i]) / denominator;
		data[i]->interval_hit_count = 0;
		data[i]->age_in_intervals++;
	}

}

int tag_based_with_prob_recomp::which_group_should_this_page_belong_to(Event const& event) {
	const int max_group = 14;
	while (data.size() <= event.get_tag() && data.size() < max_group) {
		printf("creating group %d due to tag %d\n", data.size(), event.get_tag());
		bm->add_group(0);
		group const& new_group = groups.back();
		group_data* gd = new group_data(new_group, groups);
		data.push_back(gd);
	}
	if (event.get_tag() >= max_group) {
		return max_group - 1;
	}
	return event.get_tag();
}

