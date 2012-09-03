
#include "../ssd.h"

using namespace ssd;
using namespace std;

#define THRESHOLD 6 // the number of sequential writes before we recognize the pattern as sequential

Block_manager_parallel_wearwolf_locality::Block_manager_parallel_wearwolf_locality(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel_wearwolf(ssd, ftl),
	  parallel_degree(LUN),
	  seq_write_key_to_pointers_mapping(),
	  detector(new Sequential_Pattern_Detector(THRESHOLD)),
	  strat(ROUND_ROBIN)
{
	detector->set_listener(this);
}

Block_manager_parallel_wearwolf_locality::~Block_manager_parallel_wearwolf_locality(void) {
	delete detector;
}

void Block_manager_parallel_wearwolf_locality::register_write_arrival(Event const& write) {

	if (!write.is_original_application_io()) {
		return;
	}
	if (PRINT_LEVEL > 1) {
		printf("arrival: %d  in time: %f\n", write.get_logical_address(), write.get_current_time());
	}

	ulong lb = write.get_logical_address();
	int tag = write.get_tag();
	if (tag != -1 && tag_map.count(tag) == 0) {
		tagged_sequential_write tsw(lb, write.get_size());
		tag_map[tag] = tsw;
		set_pointers_for_tagged_sequential_write(tag, write.get_current_time());
		return;
	}
	else if (tag != -1 && tag_map.count(tag) == 1) {
		return;
	}

	sequential_writes_tracking const& swt = detector->register_event(lb, write.get_current_time());
	// checks if should allocate pointers for the pattern
	if (swt.num_times_pattern_has_repeated == 0 && swt.counter == THRESHOLD) {
		if (PRINT_LEVEL > 1) {
			printf("SEQUENTIAL PATTERN IDENTIFIED!  KEY: %d \n", swt.key);
		}
		set_pointers_for_sequential_write(swt.key, write.get_current_time());
	}
	if (swt.num_times_pattern_has_repeated > 0 || swt.counter >= THRESHOLD) {
		arrived_writes_to_sequential_key_mapping[write.get_id()] = swt.key;
	}
}


Address Block_manager_parallel_wearwolf_locality::write(Event const& event) {
	int tag = event.get_tag();

	if (tag != -1 && tag_map.count(tag) == 1 && seq_write_key_to_pointers_mapping[tag_map[tag].key].num_pointers > 0) {
		return perform_sequential_write(event, tag_map[tag].key);
	}

	detector->remove_old_sequential_writes_metadata(event.get_current_time());
	//long key = detector->get_sequential_write_id(event.get_logical_address());

	if (arrived_writes_to_sequential_key_mapping.count(event.get_id()) == 1) {
		int key = arrived_writes_to_sequential_key_mapping[event.get_id()];
		assert(seq_write_key_to_pointers_mapping.count(key) == 1);
		if (seq_write_key_to_pointers_mapping[key].num_pointers > 0) {
			return perform_sequential_write(event, key);
		}
	}
	return Block_manager_parallel_wearwolf::write(event);
}

Address Block_manager_parallel_wearwolf_locality::perform_sequential_write(Event const& event, long key) {
	Address to_return;
	sequential_writes_pointers& swp = seq_write_key_to_pointers_mapping[key];
	//printf("num seq pointers left: %d\n", seq_write_key_to_pointers_mapping[key].num_pointers);

	if (strat == SHOREST_QUEUE) {
		to_return = perform_sequential_write_shortest_queue(swp);
	} else if (strat == ROUND_ROBIN) {
		to_return = perform_sequential_write_round_robin(swp);
		if (!has_free_pages(to_return)) {
			swp.cursor++;
			to_return = perform_sequential_write_shortest_queue(swp);
		}
	}
	if (!has_free_pages(to_return)) {
		schedule_gc(event.get_current_time(), -1, -1, -1);  // TODO only trigger GC if tagged need more space
		to_return = Block_manager_parallel_wearwolf::write(event);
	}
	return to_return;
}

Address Block_manager_parallel_wearwolf_locality::perform_sequential_write_shortest_queue(sequential_writes_pointers& swp) {
	pair<bool, pair<uint, uint> > best_die_id = get_free_block_pointer_with_shortest_IO_queue(swp.pointers);
	bool can_write = best_die_id.first;
	if (can_write) {
		return swp.pointers[best_die_id.second.first][best_die_id.second.second];
	}
	return Address();
}

Address Block_manager_parallel_wearwolf_locality::perform_sequential_write_round_robin(sequential_writes_pointers& swp) {
	uint cursor = swp.cursor;
	vector<vector<Address> >& p = swp.pointers;
	uint package = cursor % p.size();
	uint die = (swp.cursor / swp.pointers.size()) % swp.pointers[package].size();
	return swp.pointers[package][die];
}


// TODO: for the ONE and LUN degrees, try to do read-balancing
void Block_manager_parallel_wearwolf_locality::set_pointers_for_sequential_write(long key, double time) {
	assert(seq_write_key_to_pointers_mapping.count(key) == 0);
	if (parallel_degree == ONE) {
		seq_write_key_to_pointers_mapping[key].pointers = vector<vector<Address> >(1, vector<Address>(1));
		Address free_block = find_free_unused_block(time);
		if (free_block.valid != NONE) {
			seq_write_key_to_pointers_mapping[key].pointers[0][0] = free_block;
			seq_write_key_to_pointers_mapping[key].num_pointers++;
		}
	} else if (parallel_degree == CHANNEL) {
		seq_write_key_to_pointers_mapping[key].pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(1));
		for (uint i = 0; i < SSD_SIZE; i++) {
			Address free_block = find_free_unused_block(i, time);
			if (free_block.valid != NONE) {
				seq_write_key_to_pointers_mapping[key].pointers[i][0] = free_block;
				seq_write_key_to_pointers_mapping[key].num_pointers++;
			}
		}
	} else if (parallel_degree == LUN) {
		seq_write_key_to_pointers_mapping[key].pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(PACKAGE_SIZE));
		for (uint i = 0; i < SSD_SIZE; i++) {
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				Address free_block = find_free_unused_block(i, j, time);
				if (free_block.valid != NONE) {
					seq_write_key_to_pointers_mapping[key].pointers[i][j] = free_block;
					seq_write_key_to_pointers_mapping[key].num_pointers++;
				}
			}
		}
	}
}

void Block_manager_parallel_wearwolf_locality::set_pointers_for_tagged_sequential_write(int tag, double time) {
	int num_blocks_needed = ceil(tag_map[tag].size / (double)BLOCK_SIZE);
	int num_LUNs = SSD_SIZE * PACKAGE_SIZE;
	int num_blocks_to_allocate_now = min(num_blocks_needed, num_LUNs);
	int key = tag_map[tag].key;
	seq_write_key_to_pointers_mapping[key].tag = tag;
	vector<vector<Address> >& pointers = seq_write_key_to_pointers_mapping[key].pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(PACKAGE_SIZE));
	for (int i = 0; i < num_blocks_to_allocate_now; i++) {
		uint package = i % SSD_SIZE;
		uint die = (i / SSD_SIZE) % PACKAGE_SIZE;
		Address free_block = find_free_unused_block(package, die, time);
		if (has_free_pages(free_block)) {
			tag_map[tag].free_allocated_space += BLOCK_SIZE - free_block.page;
			seq_write_key_to_pointers_mapping[key].num_pointers++;
			pointers[package][die] = free_block;
		}
	}
}

void Block_manager_parallel_wearwolf_locality::register_write_outcome(Event const& event, enum status status) {
	long key;

	if (event.get_tag() != -1) {
		key = tag_map[event.get_tag()].key;
	} else if (arrived_writes_to_sequential_key_mapping.count(event.get_id()) == 1) {
		key = arrived_writes_to_sequential_key_mapping[event.get_id()];
		arrived_writes_to_sequential_key_mapping.erase(event.get_id());
		//key = detector->get_sequential_write_id(lb);
	}
	//int b = seq_write_key_to_pointers_mapping[key].num_pointers;
	if (seq_write_key_to_pointers_mapping.count(key) == 1 && seq_write_key_to_pointers_mapping[key].num_pointers > 0) {
		Block_manager_parent::register_write_outcome(event, status);
		page_hotness_measurer.register_event(event);
		sequential_writes_pointers& swp = seq_write_key_to_pointers_mapping[key];

		int i, j;
		if (event.get_tag() != -1) {
			i = event.get_address().package;
			j = event.get_address().die;
		}
		else if (parallel_degree == ONE) {
			i = j = 0;
		} else if (parallel_degree == CHANNEL) {
			i = event.get_address().package;
			j = 0;
		} else if (parallel_degree == LUN) {
			i = event.get_address().package;
			j = event.get_address().die;
		}

		if (strat == ROUND_ROBIN) {
			swp.cursor++;
		}

		Address selected_pointer = swp.pointers[i][j];
		assert(selected_pointer.valid == PAGE);
		selected_pointer.page++;
		swp.pointers[i][j] = selected_pointer;
		bool allocate_more_blocks = !has_free_pages(selected_pointer);

		int tag = event.get_tag();
		if (tag != -1) {
			tag_map[tag].num_written++;
			if (allocate_more_blocks && !tag_map[tag].need_more_space()) {
				allocate_more_blocks = false;
			}
			if (tag_map[tag].is_finished()) {
				int i = 0;
				i++;
				sequential_event_metadata_removed(tag_map[tag].key);
				tag_map.erase(tag);
			}
		}

		if (allocate_more_blocks) {
			Address free_block;
			if (parallel_degree == ONE) {
				free_block = find_free_unused_block(event.get_current_time());
				if (free_block.valid == NONE) {
					schedule_gc(event.get_current_time());
				}
			} else if (parallel_degree == CHANNEL) {
				free_block = find_free_unused_block(event.get_address().package, event.get_current_time());
				if (free_block.valid == NONE) {
					schedule_gc(event.get_current_time(), event.get_address().package);
				}
			} else if (parallel_degree == LUN) {
				free_block = find_free_unused_block(event.get_address().package, event.get_address().die, event.get_current_time());
				if (free_block.valid == NONE) {
					schedule_gc(event.get_current_time(), event.get_address().package, event.get_address().die);
				}
			}
			if (has_free_pages(free_block)) {
				swp.pointers[i][j] = free_block;
			} else {
				swp.num_pointers--;
			}
		}

	} else {
		Block_manager_parallel_wearwolf::register_write_outcome(event, status);
	}
}

void Block_manager_parallel_wearwolf_locality::sequential_event_metadata_removed(long key) {
	if (seq_write_key_to_pointers_mapping.count(key) == 0) {
		return;
	}
	sequential_writes_pointers& a = seq_write_key_to_pointers_mapping[key];
	printf("Returning key %d: \n", key );
	for (uint i = 0; i < a.pointers.size(); i++) {
		for (uint j = 0; j < a.pointers[i].size(); j++) {
			Address& pointer = a.pointers[i][j];
			printf("  "); pointer.print(); printf("\n");
			Block_manager_parent::return_unfilled_block(pointer);
		}
	}
	seq_write_key_to_pointers_mapping.erase(key);
}

void Block_manager_parallel_wearwolf_locality::register_erase_outcome(Event const& event, enum status status) {
	Block_manager_parallel_wearwolf::register_erase_outcome(event, status);
	int i, j;
	if (parallel_degree == ONE) {
		i = j = 0;
	} else if (parallel_degree == CHANNEL) {
		i = event.get_address().package;
		j = 0;
	} else if (parallel_degree == LUN) {
		i = event.get_address().package;
		j = event.get_address().die;
	}

	// need to figure out if a pointer is actually needed, only when there is a tag
	// need to ensure I don't crash

	map<long, sequential_writes_pointers >::iterator iter = seq_write_key_to_pointers_mapping.begin();
	for (; iter != seq_write_key_to_pointers_mapping.end(); iter++) {
		sequential_writes_pointers& swt = (*iter).second;
		if (swt.tag != -1 && !tag_map[swt.tag].need_more_space()) {
			continue;
		}
		Address& pointer = swt.pointers[i][j];
		if (!has_free_pages(pointer)) {
			if (parallel_degree == ONE) {
				(*iter).second.pointers[i][j] = find_free_unused_block(event.get_current_time());
			} else if (parallel_degree == CHANNEL) {
				(*iter).second.pointers[i][j] = find_free_unused_block(i, event.get_current_time());
			} else if (parallel_degree == LUN) {
				(*iter).second.pointers[i][j] = find_free_unused_block(i, j, event.get_current_time());
			}
			if (has_free_pages(pointer)) {
				(*iter).second.num_pointers++;
			}
		}

	}
	//check_if_should_trigger_more_GC(event.get_current_time());
}

Block_manager_parallel_wearwolf_locality::sequential_writes_pointers::sequential_writes_pointers()
	: num_pointers(0),
	  pointers(),
	  cursor(rand() % 100),
	  tag(-1)
{}

/*void Block_manager_parallel_wearwolf_locality::check_if_should_trigger_more_GC(double start_time) {
	map<long, vector<vector<Address> > >::iterator iter = seq_write_key_to_pointers_mapping.begin();

	int a, b;
	if (parallel_degree == ONE) {
		a = b = 0;
	} else if (parallel_degree == CHANNEL) {
		a = PACKAGE_SIZE;
		b = 0;
	} else if (parallel_degree == LUN) {
		a = PACKAGE_SIZE;
		b = DIE_SIZE;
	}

	for (; iter != seq_write_key_to_pointers_mapping.end(); iter++) {
		for (int i = 0; i < a; i++) {
			for (int j = 0; j < b; j++) {
				if ((*iter).second[i][j].page >= BLOCK_SIZE) {
					if (parallel_degree == ONE) {
						perform_gc(start_time);
					} else if (parallel_degree == CHANNEL) {
						find_free_unused_block(i, start_time);
					} else if (parallel_degree == LUN) {
						find_free_unused_block(i, j, start_time);
					}
				}
			}
		}
	}
}*/

