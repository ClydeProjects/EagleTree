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

Block_Manager_Tag_Groups::Block_Manager_Tag_Groups()
: Block_manager_parent(),
  free_block_pointers_tags()
{}

void Block_Manager_Tag_Groups::register_write_arrival(Event const& e) {
	int t = e.get_tag();
	if (t == UNDEFINED || free_block_pointers_tags.count(t) == 1) {
		return;
	}
	free_block_pointers_tags[t] = vector<vector<Address> >(SSD_SIZE, vector<Address>(PACKAGE_SIZE));
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			free_block_pointers_tags.at(t)[i][j] = find_free_unused_block(i, j, e.get_current_time());
		}
	}
}

/*void Block_Manager_Tag_Groups::increment_pointer_and_find_free(Address& block, double time) {
	increment_pointer(block);
	if (!has_free_pages(block)) {
		Address free_pointer = find_free_unused_block(block.package, block.die, time);
		if (has_free_pages(free_pointer)) {
			block = free_pointer;
		}
	}
}*/

void Block_Manager_Tag_Groups::register_write_outcome(Event const& event, enum status status) {
	Block_manager_parent::register_write_outcome(event, status);
	if (event.get_tag() == UNDEFINED) {
		return;
	}
	int tag = event.get_tag();
	int p = event.get_address().package;
	int d = event.get_address().die;
	Address& block = free_block_pointers_tags.at(tag)[p][d];
	if (block.compare(event.get_address()) == PAGE) {
		increment_pointer(block);
		if (!has_free_pages(block)) {
			free_block_pointers_tags[tag][p][d] = find_free_unused_block(p, d, event.get_current_time());
		}
		return;
	}

	for (auto t : free_block_pointers_tags) {
		Address& a_block = free_block_pointers_tags[t.first][p][d];
		if (a_block.compare(event.get_address()) == PAGE) {
			increment_pointer(a_block);
			free_block_pointers_tags[t.first][p][d] = a_block;
			if (!has_free_pages(a_block)) {
				Address free_pointer = find_free_unused_block(p, d, event.get_current_time());
				if (has_free_pages(free_pointer)) {
					free_block_pointers_tags[t.first][p][d] = free_pointer;
					break;
				}
			}
		}
	}
}

void Block_Manager_Tag_Groups::register_erase_outcome(Event& event, enum status status) {
	Block_manager_parent::register_erase_outcome(event, status);
	int p = event.get_address().package;
	int d = event.get_address().die;

	for (auto t : free_block_pointers_tags) {
		Address& a = free_block_pointers_tags.at(t.first)[p][d];
		if (!has_free_pages(a)) {
			free_block_pointers_tags.at(t.first)[p][d] = find_free_unused_block(p, d, event.get_current_time());
			//return;
		}
	}

	if (!has_free_pages(free_block_pointers[p][d])) {
		free_block_pointers[p][d] = find_free_unused_block(p, d, event.get_current_time());
	}
}

// handle garbage_collection case. Based on range.
Address Block_Manager_Tag_Groups::choose_best_address(Event& write) {

	if (write.get_tag() == UNDEFINED && write.is_original_application_io()) {
		return get_free_block_pointer_with_shortest_IO_queue();
	}

	int tag = write.get_tag();
	if (tag == UNDEFINED && write.is_garbage_collection_op()) {
		long lba = write.get_logical_address();
		for (auto& i : free_block_pointers_tags) {
			if (i.first > tag && i.first <= lba) {
				tag = i.first;
			}
		}
		write.set_tag(tag);
	}

	pair<bool, pair<int, int> > result = get_free_block_pointer_with_shortest_IO_queue(free_block_pointers_tags.at(tag));
	if (result.first) {
		return free_block_pointers_tags.at(tag)[result.second.first][result.second.second];
	}
	return get_free_block_pointer_with_shortest_IO_queue();
}

Address Block_Manager_Tag_Groups::choose_any_address(Event const& write) {
	Address a = get_free_block_pointer_with_shortest_IO_queue();
	if (has_free_pages(a)) {
		return a;
	}
	for (auto t : free_block_pointers_tags) {
		for (int i = 0; i < t.second.size(); i++) {
			for (int j = 0; j < t.second[i].size(); j++) {
				if (has_free_pages(t.second[i][j])) {
					return t.second[i][j];
				}
			}
		}
	}
	return Address();
}

void Block_Manager_Tag_Groups::print() const{
	int mixed = 0;

	for (auto i : free_block_pointers_tags) {
		cout << i.first << endl;
		for (auto p : i.second) {
			for (auto d : p) {
				printf("\t");
				d.print();
				printf("\n");
			}
		}
	}

	map<int, int> histogram;
	for (int p = 0; p < SSD_SIZE; p++) {
		for (int d = 0; d < PACKAGE_SIZE; d++) {
			for (int pl = 0; pl < DIE_SIZE; pl++) {
				for (int b = 0; b < PLANE_SIZE; b++) {
					//Block* block = ssd->get_package(p)->get_die(d)->get_plane(p)->get_block(b);
					int tag = UNDEFINED;
					for (int pa = 0; pa < BLOCK_SIZE; pa++) {
						Address a = Address(p, d, pl, b, pa, PAGE);
						int la = ftl->get_logical_address(a.get_linear_address());
						if (la == UNDEFINED) {
							continue;
						}
						map<int, vector<vector<Address> > >::const_iterator i = free_block_pointers_tags.lower_bound(la);
						if ( (*i).first != la ) {
							i--;
						}
						if (tag == UNDEFINED) {
							tag = (*i).first;
							histogram[tag]++;
						}
						else if (tag != (*i).first) {
							// mixed data
							mixed++;
							histogram[tag]--;
							break;
						}
					}
				}
			}
		}
	}

	for (auto i : histogram) {
		cout << i.first << ": " << i.second << endl;
	}
	cout << "mixed: " << mixed << endl;
	cout << "num blocks: " << NUMBER_OF_ADDRESSABLE_BLOCKS() << endl;


}


