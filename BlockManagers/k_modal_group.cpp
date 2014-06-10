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

group::group(double prob, double size, Block_manager_parent* bm, Ssd* ssd) : prob(prob), size(size), OP(0), OP_greedy(0),
			OP_prob(0), OP_average(0), free_blocks(bm), next_free_blocks(bm), block_ids(), stats() {
	double PBA = NUMBER_OF_ADDRESSABLE_PAGES();
	double LBA = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	get_prob_op(PBA, LBA);
	get_greedy_op(PBA, LBA);
	get_average_op(PBA, LBA);
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			Block* block1 = ssd->get_package(free_blocks.blocks[i][j].package)->get_die(free_blocks.blocks[i][j].die)->get_plane(free_blocks.blocks[i][j].plane)->get_block(free_blocks.blocks[i][j].block);
			block_ids.insert(block1);
			Block* block2 = ssd->get_package(next_free_blocks.blocks[i][j].package)->get_die(next_free_blocks.blocks[i][j].die)->get_plane(next_free_blocks.blocks[i][j].plane)->get_block(next_free_blocks.blocks[i][j].block);
			block_ids.insert(block2);
		}
	}
}

void group::print() {
	printf("size: %d\tprob: %f\tOP_greedy: %d\tOP_prob: %d\tOP_avg: %d\tOP: %d\tamp: %f\tblocks: %d\tpages: %d\n",
			(int)size, prob, (int)OP_greedy, (int) OP_prob, (int) OP_average, (int)OP, get_write_amp(opt), prob, block_ids.size(), block_ids.size() * BLOCK_SIZE);
	stats.print();
	free_blocks.print();
}

double group::get_prob_op(double PBA, double LBA) {
	return OP_prob = (PBA - LBA) * prob;
}

double group::get_greedy_op(double PBA, double LBA) {
	return OP_greedy = (PBA / LBA - 1) * size;
}

double group::get_average_op(double PBA, double LBA) {
	double weight = 0.5;
	return OP_average = OP = (get_greedy_op(PBA, LBA) * weight + get_prob_op(PBA, LBA) * (1 - weight));
}

double group::get_write_amp(write_amp_choice choice) {
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

vector<group> group::iterate(vector<group> const& groups) {
	double PBA = NUMBER_OF_ADDRESSABLE_PAGES();
	double LBA = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	vector<double> incrementals(groups.size());
	vector<group> groups_opt = groups;

	for (unsigned int i = 0; i < groups.size(); i++) {
		groups_opt[i].OP = groups[i].OP_average;
		incrementals[i] = groups[i].OP_average / 2;
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
	return groups_opt;
}

void group::print(vector<group>& groups) {
	for (unsigned int i = 0; i < groups.size(); i++) {
		groups[i].print();
	}
}

void group::init_stats(vector<group>& groups) {
	for (unsigned int i = 0; i < groups.size(); i++) {
		groups[i].stats = group_stats();
	}
}

void group::group_stats::print() {
	printf("\tnum_gc_in_group:\t%d\n", num_gc_in_group);
	printf("\tnum_writes_to_group:\t%d\n", num_writes_to_group);
	printf("\tnum_gc_writes_to_group:\t%d\n", num_gc_writes_to_group);
}

Block* group::get_gc_victim() {
	int min = BLOCK_SIZE;
	Block* victim = NULL;
	for (auto b : block_ids) {
		if (b->get_pages_valid() < min && b->get_state() == ACTIVE) {
			min = b->get_pages_valid();
			victim = b;
		}
	}
	return victim;
}

Block* group::get_gc_victim(int package, int die) {
	int min = BLOCK_SIZE;
	Block* victim = NULL;
	for (auto b : block_ids) {
		Address a = Address(b->get_physical_address(), BLOCK);
		if (b->get_pages_valid() < min && b->get_state() == ACTIVE && a.package == package && a.die == die) {
			min = b->get_pages_valid();
			victim = b;
		}
	}
	if (victim->get_physical_address() == 36480) {
		int i = 0;
		i++;
	}
	return victim;
}

bool group::is_starved() const {
	int num_live_blocks = 0;
	return free_blocks.get_num_free_blocks() < SSD_SIZE * PACKAGE_SIZE / 2;
}

