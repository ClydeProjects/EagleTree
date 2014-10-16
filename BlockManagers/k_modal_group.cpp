/*
 * k_modal_group.cpp
 *
 *  Created on: Jun 1, 2014
 *      Author: niv
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>
#include <algorithm>
#include "../ssd.h"

using namespace ssd;

vector<int> group::mapping_pages_to_groups =  vector<int>();
vector<int> group::mapping_pages_to_tags =  vector<int>();
int group::num_groups_that_need_more_blocks = 0;
int group::num_groups_that_need_less_blocks = 0;
int group::num_writes_since_last_regrouping = 0;
int group::id_generator = 0;
int group::overprov_allocation_strategy = 1;  // 0 is iterative, 1 is closed form


group::group(double prob, double size, Block_manager_parent* bm, Ssd* ssd, int index) : prob(prob), size(size), offset(0), OP(0), OP_greedy(0),
			OP_prob(0), OP_average(0), free_blocks(bm), next_free_blocks(bm), block_ids(), blocks_being_garbage_collected(), stats(), num_pages(0), actual_prob(prob),
			num_pages_per_die(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
			num_blocks_ever_given(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
			num_blocks_per_die(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
			blocks_queue_per_die(SSD_SIZE, vector<vector<Block*> >(PACKAGE_SIZE, vector<Block*>())),
			stats_gatherer(StatisticsGatherer()),
			index(index), ssd(ssd), id(id_generator++), num_app_writes(0) {
	double PBA = NUMBER_OF_ADDRESSABLE_PAGES();
	double LBA = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	get_prob_op(PBA, LBA);
	get_greedy_op(PBA, LBA);
	get_average_op(PBA, LBA);
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			if (free_blocks.blocks[i][j].valid == PAGE) {
				Block* block1 = ssd->get_package(free_blocks.blocks[i][j].package)->get_die(free_blocks.blocks[i][j].die)->get_plane(free_blocks.blocks[i][j].plane)->get_block(free_blocks.blocks[i][j].block);
				block_ids.insert(block1);
				num_blocks_per_die[i][j] += 1;
			}
			if (next_free_blocks.blocks[i][j].valid == PAGE) {
				Block* block2 = ssd->get_package(next_free_blocks.blocks[i][j].package)->get_die(next_free_blocks.blocks[i][j].die)->get_plane(next_free_blocks.blocks[i][j].plane)->get_block(next_free_blocks.blocks[i][j].block);
				block_ids.insert(block2);
				num_blocks_per_die[i][j] += 1;
			}
		}
	}
}

group::group() : prob(0), size(0), offset(), OP(0), OP_greedy(0),
		OP_prob(0), OP_average(0), free_blocks(), next_free_blocks(), block_ids(), blocks_being_garbage_collected(), stats(), num_pages(0), actual_prob(0),
		num_pages_per_die(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
		num_blocks_ever_given(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
		num_blocks_per_die(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
		stats_gatherer(StatisticsGatherer()),
		index(index), ssd(NULL), id(id_generator++)
{}

void group::print() const {
	printf("id: %d\t", (int)index);
	printf("size: %d\t", (int)size);
	printf("actual num pages: %d\t", (int)num_pages);
	printf("prob: %f\t", prob);
	printf("hit_rate: %f\t", get_normalized_hits_per_page());
	printf("amp: %f\t", get_write_amp(opt));
	printf("blocks: %d\t", (int)block_ids.size());
	printf("pages: %d\t", (int)block_ids.size() * BLOCK_SIZE);
	printf("OP: %d\t", (int)OP);
	printf("OP_greedy: %d\t", (int)OP_greedy);
	printf("OP_prob: %d\t", (int)OP_prob);
	printf("OP_avg: %d\t", (int)OP_average);
	printf("\n");
	stats.print();
	printf("\tnum ongoing gc in group: %d\n", this->blocks_being_garbage_collected.size());
	if (this->in_equilbirium()) {
		printf("\tin equib\n");
	}
	else if (this->needs_more_blocks()) {
		int diff = (OP + size) / BLOCK_SIZE - block_ids.size();
		printf("\tneeds blocks: %d\n", diff);
	}
	else {
		int diff = block_ids.size() - (OP + size) / BLOCK_SIZE;
		printf("\texcess blocks: %d\n", diff);
	}

	string title = "gc_for_diff_groups_" + std::to_string(id);
	double avg = StatisticData::get_weighted_avg_of_col2_in_terms_of_col1(title, 0, 1);
	printf("\tavg num live blocks over time: %f\n", avg);
	print_tags_per_group();
	free_blocks.print();
	//stats_gatherer.print();
	//print_die_spesific_info();
	//print_blocks_valid_pages_per_die();
}

void group::print_tags_per_group() const {
	map<int, int> tag_map;
	for (int i = 0; i < mapping_pages_to_groups.size(); i++) {
		if (mapping_pages_to_groups[i] == index) {
			int tag = mapping_pages_to_tags[i];
			tag_map[tag]++;
		}
	}
	printf("\ttags ");
	for (auto t : tag_map) {
		printf("\t%d\t%d", t.first, t.second);
	}
	printf("\n");
}

void group::print_die_spesific_info() const {
	for (int i = 0; i < num_pages_per_die.size(); i++) {
		for (int j = 0; j < num_pages_per_die[i].size(); j++) {
			printf("\tnum pages in %d %d: %d  %d   %d    %d    %d    %d     %d\n", i, j,
					num_pages_per_die[i][j],
					num_blocks_per_die[i][j],
					num_blocks_ever_given[i][j],
					stats_gatherer.num_erases_per_LUN[i][j],
					stats_gatherer.num_writes_per_LUN[i][j],
					stats_gatherer.num_gc_writes_per_LUN_origin[i][j],
					stats_gatherer.num_gc_writes_per_LUN_destination[i][j]);
		}
	}
}

void group::print_blocks_valid_pages() const {
	for (auto b : block_ids) {
		printf("%d ", b->get_pages_valid());
	}
	printf("\n");
}

void group::print_blocks_valid_pages_per_die() const {
	for (int i = 0; i < blocks_queue_per_die.size(); i++) {
		for (int j = 0; j < blocks_queue_per_die[i].size(); j++) {
			printf("%d %d: ", i, j);
			for (int b = 0; b < blocks_queue_per_die[i][j].size(); b++) {
				printf("%d ", blocks_queue_per_die[i][j][b]->get_pages_valid());
			}
			//assert(blocks_queue_per_die[i][j].size() == num_blocks_per_die[i][j]);
			printf("\n");
			//printf("%d %d\n", blocks_queue_per_die[i][j].size(), num_blocks_per_die[i][j]);
		}
	}
}

double group::get_prob_op(double PBA, double LBA) {
	return OP_prob = (PBA - LBA) * prob;
}

double group::get_greedy_op(double PBA, double LBA) {
	return OP_greedy = (PBA / LBA - 1) * size;
}

double group::get_average_op(double PBA, double LBA) {
	double weight = 0.5;
	double greedy = get_greedy_op(PBA, LBA);
	double prob = get_prob_op(PBA, LBA);
	return OP_average = OP = greedy * weight + prob * (1 - weight);
}

double group::get_write_amp(write_amp_choice choice) const {
	double OP_chosen = 0;
	if (choice == opt) {
		OP_chosen = OP;
	}
	else if (choice == greedy) {
		OP_chosen = OP_greedy;
	} else {
		OP_chosen = OP_prob;
	}
	double over_prov = OP_chosen / size;
	double page_eff = exp(- 0.9 * over_prov) / (over_prov + 1);
	double write_amp = 1.0 / (1.0 - page_eff);
	return write_amp;
}

double group::get_average_write_amp(vector<group>& groups, write_amp_choice choice) {
	double weighted_avg = 0;
	for (unsigned int i = 0; i < groups.size(); i++) {
		weighted_avg += groups[i].prob * groups[i].get_write_amp(choice);
	}
	return weighted_avg;
}

vector<group> group::allocate_op(vector<group> const& groups) {
	if (overprov_allocation_strategy == 0) {
		return iterate(groups);
	}
	else if (overprov_allocation_strategy == 1) {
		return closed_form_method(groups);
	}
	else {
		return iterate_except_first(groups);
	}
}

vector<group> group::closed_form_method(vector<group> const& groups) {
	double PBA = NUMBER_OF_ADDRESSABLE_PAGES(), LBA = 0;
	for (auto g : groups) {
		LBA += g.num_pages;
	}
	return closed_form_method(groups, LBA, PBA);
}

vector<group> group::closed_form_method(vector<group> const& groups, int LBA, int PBA) {
	vector<group> groups_opt = groups;
	for (unsigned int i = 0; i < groups.size(); i++) {
		groups_opt[i].get_average_op(PBA, LBA);
	}
	return groups_opt;
}

vector<group> group::iterate(vector<group> const& groups) {

	double PBA = NUMBER_OF_ADDRESSABLE_PAGES();
	double LBA = 0;

	for (auto g : groups) {
		LBA += g.num_pages;
	}

	vector<double> incrementals(groups.size());
	vector<group> groups_opt = groups;

	double total_OP = 0;
	for (unsigned int i = 0; i < groups.size(); i++) {
		groups_opt[i].get_average_op(PBA, LBA);
		groups_opt[i].OP = groups[i].OP_average;
		incrementals[i] = groups[i].OP_average / 2;
		total_OP += groups_opt[i].OP;
	}
	if (total_OP < NUMBER_OF_ADDRESSABLE_PAGES() * (1 - OVER_PROVISIONING_FACTOR)) {
		double diff = NUMBER_OF_ADDRESSABLE_PAGES() * (1 - OVER_PROVISIONING_FACTOR) - total_OP;
		//printf("extra OP %f\n", diff);
		for (unsigned int i = 0; i < groups.size(); i++) {
			groups_opt[i].OP += diff / groups.size();
		}
	}

	double current_write_amp = get_average_write_amp(groups_opt);
	double new_write_amp = 0;
	int last_elem_index = groups.size() - 1;
	for (int i = 0; i < 100; i++) {
		for (unsigned int i = 0; i < groups_opt.size() - 1; i++) {

			groups_opt[i].OP += incrementals[i];
			groups_opt[last_elem_index].OP -= incrementals[i];
			new_write_amp = get_average_write_amp(groups_opt);
			if (new_write_amp < current_write_amp && groups_opt[i].OP < PBA - LBA && groups_opt[last_elem_index].OP > 0) {
				current_write_amp = new_write_amp;
				continue;
			}

			groups_opt[i].OP -= 2 * incrementals[i];
			groups_opt[last_elem_index].OP += 2 * incrementals[i];
			new_write_amp = get_average_write_amp(groups_opt);
			if (new_write_amp < current_write_amp && groups_opt[i].OP > 0 && groups_opt[last_elem_index].OP < PBA - LBA) {
				current_write_amp = new_write_amp;
				continue;
			}
			groups_opt[i].OP += incrementals[i];
			groups_opt[last_elem_index].OP -= incrementals[i];
			incrementals[i] /= 2;
		}
	}

	/*for (int i = 1; i < groups_opt.size(); i++) {
		int transfer = groups_opt[i].OP * 0.02;
		printf("transfer from %d: %d\n", i, transfer);
		groups_opt[0].OP += transfer;
		groups_opt[i].OP -= transfer;
	}*/

	return groups_opt;
}

vector<group> group::iterate_except_first(vector<group> const& groups) {

	if (groups.size() == 1) {
		return closed_form_method(groups);
	}

	vector<group> sorted = groups;
	sort(sorted.begin(), sorted.end(), [](group const& a, group const& b) { return a.get_normalized_hits_per_page() < b.get_normalized_hits_per_page(); });

	double hit_rate1 = sorted[0].get_normalized_hits_per_page();
	double hit_rate2 = sorted[1].get_normalized_hits_per_page();

	//printf("%f  %f\n", hit_rate1, hit_rate2);

	if (hit_rate1 / hit_rate2 > 0.05 || sorted[0].num_app_writes < BLOCK_SIZE * 100) {
		return closed_form_method(groups);
	}

	for (int i = 0; i < sorted.size(); i++) {
		printf("%d: %d\t%d\n", sorted[i].index, (int)sorted[i].OP, (int)groups[sorted[i].index].OP);
	}
	printf("\n");

	printf("%f\n", groups[0].prob);
	vector<group> copy;

	double total_prob = 1 - sorted[0].prob;
	for (int i = 1; i < sorted.size(); i++) {
		copy.push_back(sorted[i]);
		copy[i-1].prob /= total_prob;
	}
	assert(groups.size() == copy.size() + 1);

	double PBA = NUMBER_OF_ADDRESSABLE_PAGES();
	double LBA = 0;

	for (auto g : groups) {
		LBA += g.num_pages;
	}

	//printf("LBA: %f   PBA %f\n", LBA, PBA);
	int OP = PBA - LBA;
	sorted[0].OP = max(sorted[0].OP * 0.95, sorted[0].size * 0.05);
	int closed_form_LBA = LBA - sorted[0].size;
	int closed_form_PBA = PBA - sorted[0].size - sorted[0].OP;
	printf("total OP: %d   OP given to lowest: %d\n", OP, (int)sorted[0].OP);
	printf("feeding into closed form:  LBA: %d   PBA: %d  OP: %d\n", closed_form_LBA, closed_form_PBA, closed_form_PBA - closed_form_LBA);
	copy = closed_form_method(copy, closed_form_LBA, closed_form_PBA);

	vector<group> copy2(groups.size());
	copy2[sorted[0].index] = sorted[0];
	for (int i = 0; i < copy.size(); i++) {
		copy2[copy[i].index] = copy[i];
		copy2[copy[i].index].prob *= total_prob;
	}

	int total = 0;
	for (int i = 0; i < copy2.size(); i++) {
		printf("%d: %d\n", copy2[i].index, (int)copy2[i].OP);
		total += copy2[i].OP;
	}
	printf("total: %d\n", total);

	return copy2;
}

void group::print(vector<group> const& groups) {
	vector<group> copy = groups;
	sort(copy.begin(), copy.end(), [](group const& a, group const& b) { return a.prob / a.size < b.prob / b.size; });
	for (unsigned int i = 0; i < copy.size(); i++) {
		copy[i].print();
	}
}

void group::print_tags_distribution(vector<group> const& groups) {
	map<int, int> tag_to_total;
	vector<map<int, int> > per_group(groups.size(), map<int, int>());
	for (int i = 0; i < mapping_pages_to_groups.size(); i++) {
		int tag = mapping_pages_to_tags[i];
		tag_to_total[tag]++;
		int group_id = mapping_pages_to_groups[i];
		if (group_id != UNDEFINED) {
			assert(group_id == groups[group_id].index);
			per_group[group_id][tag]++;
		}

		/*if (mapping_pages_to_groups[i] == index) {
			int tag = mapping_pages_to_tags[i];
			tag_map[tag]++;
		}*/
	}

	for (auto g : tag_to_total) {
		printf("\t%d:\t%d\n", g.first, g.second);
	}
	for (int i = 0; i < per_group.size(); i++) {
		printf("%d\n", i);
		for (auto tags : per_group[i]) {
			double fraction = tags.second / (double) tag_to_total[tags.first];
			printf("\t%d:\t%d\t%d\n", tags.first, tags.second, (int)(fraction * 100));
		}
	}

}



void group::init_stats(vector<group>& groups) {
	for (unsigned int i = 0; i < groups.size(); i++) {
		groups[i].stats = group_stats();
		groups[i].stats_gatherer = StatisticsGatherer();
		string title = "gc_for_diff_groups_" + std::to_string(groups[i].id);
		StatisticData::clean(title);
	}
}

double group::get_avg_pages_per_block_per_die() const {
	double avg = 0;
	double count = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			if (num_blocks_per_die[i][j] > 0) {
				/*if (index == 1) {
					printf("%d  /  %d\n", num_pages_per_die[i][j], num_blocks_per_die[i][j]);
				}*/
				avg += (double)num_pages_per_die[i][j] / (double)num_blocks_per_die[i][j];
				count++;
			}
		}
	}
	return avg / count;
}

double group::get_avg_pages_per_die() const {
	double avg = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			avg += num_pages_per_die[i][j];
		}
	}
	return avg / (SSD_SIZE * PACKAGE_SIZE);
}

double group::get_avg_blocks_per_die() const {
	double avg = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			avg += num_blocks_per_die[i][j];
		}
	}
	return avg / (SSD_SIZE * PACKAGE_SIZE);
}

double group::get_min_pages_per_die() const {
	double min = NUMBER_OF_ADDRESSABLE_PAGES();
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			if (min > num_pages_per_die[i][j]) {
				min = num_pages_per_die[i][j];
			}
		}
	}
	return min;
}

bool group::is_stable() {
	return num_writes_since_last_regrouping > NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR * 0.25;
}

void group::group_stats::print() const {
	printf("\tnum_gc_in_group:\t%d", num_gc_in_group);
	printf("\tnum_writes_to_group:\t%d", num_writes_to_group);
	printf("\tnum_gc_writes_to_group:\t%d\n", num_gc_writes_to_group);

	printf("\tnum_requested_gc:\t%d", num_requested_gc);
	printf("\tnum_requested_gc_starved:\t%d", num_requested_gc_starved);
	printf("\tnum_requested_gc_to_balance:\t%d\n", num_requested_gc_to_balance);

	double write_amp = (num_writes_to_group + num_gc_writes_to_group) / (double)num_writes_to_group;
	printf("\tactual write amp:\t%f", write_amp);
	printf("\tfactor:\t%f\n", (double) num_gc_writes_to_group  / (double)num_gc_in_group );
	printf("\tmigrated in: %d", migrated_in);
	printf("\tmigrated out: %d\n", migrated_out);
}

void group::register_write_outcome(Event const& event) {
	mapping_pages_to_tags[event.get_logical_address()] = event.get_tag();
	stats_gatherer.register_completed_event(event);
	num_app_writes++;
	if (event.get_address().page == 0) {
		Address a = event.get_address();
		num_blocks_ever_given[a.package][a.die]++;
		Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
		blocks_queue_per_die[a.package][a.die].push_back(block);
	}

}



void group::register_erase_outcome(Event& event) {
	Address a = event.get_address();
	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	assert(block_ids.count(block) == 1);
	block_ids.erase(block);
	blocks_being_garbage_collected.erase(block);
	stats.num_gc_in_group++;
	stats_gatherer.register_completed_event(event);
	num_blocks_per_die[a.package][a.die]--;
	for (int i = 0; i < blocks_queue_per_die[a.package][a.die].size(); i++) {
		if (blocks_queue_per_die[a.package][a.die][i] == block) {
			blocks_queue_per_die[a.package][a.die].erase(blocks_queue_per_die[a.package][a.die].begin() + i);
			break;
		}
	}
	//vec.erase(std::remove(vec.begin(), vec.end(), int_to_remove), vec.end());
	//blocks_queue_per_die[a.package][a.die].erase()
}

Block* group::get_gc_victim_LRU(int package, int die) const {
	Block* b = blocks_queue_per_die[package][die].front();
	Address a = Address(b->get_physical_address(), BLOCK);
	for (int i = 0; i < blocks_queue_per_die[package][die].size(); i++) {
		if (b->get_state() == ACTIVE && a.package == package && a.die == die) {
			return b;
		}
	}
	return NULL;
}

Block* group::get_gc_victim_window_greedy(int package, int die) const {
	double window_factor = PLANE_SIZE;
	int win1 = 0.1 * PLANE_SIZE, win2 = blocks_queue_per_die[package][die].size();
	int window_size = min(win1, win2);
	int selected_index = 0;
	int min_num_live_blocks = BLOCK_SIZE;
	for (int i = 0; i < window_size; i++) {
		Block* b = blocks_queue_per_die[package][die][i];
		if (b->get_state() == ACTIVE && min_num_live_blocks >= b->get_pages_valid()) {
			Address a = Address(b->get_physical_address(), BLOCK);
			assert(a.package == package && a.die == die);
			selected_index = i;
			min_num_live_blocks = b->get_pages_valid();
		}
	}
	return blocks_queue_per_die[package][die][selected_index];
}

Block* group::get_gc_victim_greedy(int package, int die) const {
	int min = BLOCK_SIZE;
	Block* victim = NULL;
	for (auto b : block_ids) {
		Address a = Address(b->get_physical_address(), BLOCK);
		if (b->get_pages_valid() < min && b->get_state() == ACTIVE && a.package == package && a.die == die) {
			min = b->get_pages_valid();
			victim = b;
			/*if (id == 0) {
				cout << b->get_pages_valid() << " ";
			}*/
		}
	}
	/*if (id == 0) {
		cout << endl;
	}*/
	return victim;
}

/*bool group::is_starved() const {
	return free_blocks.get_num_free_blocks()+ next_free_blocks.get_num_free_blocks() < Block_Manager_Groups::starvation_threshold;
}*/

bool group::is_starved() const {
	return free_blocks.get_num_free_blocks() < SSD_SIZE * PACKAGE_SIZE * 0.5;
}


bool group::needs_more_blocks() const {
	return block_ids.size() * BLOCK_SIZE <= OP + size;
}

int group::needs_how_many_blocks() const {
	return (OP + size - block_ids.size() * BLOCK_SIZE) / BLOCK_SIZE;
}

void group::accept_block(Address block_addr) {
	if (free_blocks.blocks[block_addr.package][block_addr.die].page == BLOCK_SIZE || free_blocks.blocks[block_addr.package][block_addr.die].valid != PAGE) {
		free_blocks.blocks[block_addr.package][block_addr.die] = block_addr;
	}
	else {
		next_free_blocks.blocks[block_addr.package][block_addr.die] = block_addr;
	}
	//else assert(false);
	Block* block = ssd->get_package(block_addr.package)->get_die(block_addr.die)->get_plane(block_addr.plane)->get_block(block_addr.block);
	block_ids.insert(block);
	num_blocks_per_die[block_addr.package][block_addr.die]++;

	string title = "gc_for_diff_groups_" + std::to_string(id);
	int num_free_pointers = free_blocks.get_num_free_blocks() + next_free_blocks.get_num_free_blocks();
	StatisticData::register_statistic(title, {
			new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
			new Integer(num_free_pointers),
	});

	StatisticData::register_field_names(title, {
			"time",
			"num-live-blocks"
	});
}

void group::retire_active_blocks(double current_time) {
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			Address a = free_blocks.blocks[i][j];
			Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
			block_ids.erase(block);
			a = next_free_blocks.blocks[i][j];
			block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
			block_ids.erase(block);
		}
	}
	free_blocks.retire(current_time);
	next_free_blocks.retire(current_time);
}

bool group::in_equilbirium() const {
	int diff = abs(block_ids.size() * BLOCK_SIZE - (OP + size));
	if (diff > BLOCK_SIZE * 20) {
		return false;
	}
	return true;
}

void group::count_num_groups_that_need_more_blocks(vector<group> const& groups) {
	num_groups_that_need_more_blocks = 0;
	num_groups_that_need_less_blocks = 0;
	int num_not_in_equib_need_less_blocks = 0;
	for (int i = 0; i < groups.size(); i++) {
		if (!groups[i].in_equilbirium() && groups[i].needs_more_blocks()) {
			num_groups_that_need_more_blocks++;
		}
		else if (!groups[i].in_equilbirium() && !groups[i].needs_more_blocks()) {
			num_groups_that_need_less_blocks++;
		}
	}
}


bool group::in_total_equilibrium(vector<group> const& groups, int group_id) {
	if (groups[group_id].in_equilbirium()) {
		return true;
	}

	if (groups[group_id].needs_more_blocks() && num_groups_that_need_less_blocks > 0) {
		return false;
	}
	if (!groups[group_id].needs_more_blocks() && num_groups_that_need_more_blocks > 0) {
		return false;
	}
	return true;
}
