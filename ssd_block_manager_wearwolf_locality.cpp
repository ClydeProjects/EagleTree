
#include "ssd.h"

using namespace ssd;
using namespace std;

#define THRESHOLD 6 // the number of sequential writes before we recognize the pattern as sequential

Block_manager_parallel_wearwolf_locality::Block_manager_parallel_wearwolf_locality(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel_wearwolf(ssd, ftl),
	  parallel_degree(LUN),
	  detector(new Sequential_Pattern_Detector()),
	  recorder(new Sequential_Pattern_Detector())
{
	recorder->set_listener(this);
}

Block_manager_parallel_wearwolf_locality::~Block_manager_parallel_wearwolf_locality(void) {
	delete detector;
	delete recorder;
}

void Block_manager_parallel_wearwolf_locality::register_write_arrival(Event const& write) {

	if (!write.is_original_application_io()) {
		return;
	}
	long lb = write.get_logical_address();

	detector->register_event(lb, write.get_current_time());
	//printf("arrival: %d  in time: %f\n", write.get_logical_address(), write.get_current_time());
	if (detector->get_num_times_pattern_has_repeated(lb) == 0 && detector->get_current_offset(lb) == THRESHOLD) {
		printf("SEQUENTIAL PATTERN IDENTIFIED!\n");
		long key = detector->get_sequential_write_id(lb);
		set_pointers_for_sequential_write(key, write.get_current_time());
	}
}


pair<double, Address> Block_manager_parallel_wearwolf_locality::write(Event & event) {
	ulong lb = event.get_logical_address();
	long key = recorder->get_sequential_write_id(lb);
	bool key_exists = seq_write_key_to_pointers_mapping.count(key) == 1;

	if (event.get_id() == 248) {
		int i = 0;
		i++;
	}

	if (!key_exists  || (key_exists && seq_write_key_to_pointers_mapping[key].num_pointers == 0)) {
		return Block_manager_parallel_wearwolf::write(event);
	} else {
		printf("performing seq write for: %d  key: %d  id: %d \n", event.get_logical_address(), key, event.get_id());
		return perform_sequential_write(key, event.get_current_time());
	}
}

pair<double, Address> Block_manager_parallel_wearwolf_locality::perform_sequential_write(long key, double current_time) {
	pair<double, Address> to_return;
	vector<vector<Address> > pointers = seq_write_key_to_pointers_mapping[key].pointers;
	printf("num seq pointers left: %d\n", seq_write_key_to_pointers_mapping[key].num_pointers);
	pair<bool, pair<uint, uint> > best_die_id = Block_manager_parent::get_free_die_with_shortest_IO_queue(pointers);
	bool can_write = best_die_id.first;

	if (can_write) {
		to_return.second = pointers[best_die_id.second.first][best_die_id.second.second];
		to_return.first = in_how_long_can_this_event_be_scheduled(to_return.second, current_time);
	} else {
		to_return.first = 1;
	}
	return to_return;
}



// TODO: for the ONE and LUN degrees, try to do read-balancing
void Block_manager_parallel_wearwolf_locality::set_pointers_for_sequential_write(long key, double time) {
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



void Block_manager_parallel_wearwolf_locality::register_write_outcome(Event const& event, enum status status) {
	long lb = event.get_logical_address();
	long key = recorder->get_sequential_write_id(lb);
	if (seq_write_key_to_pointers_mapping.count(key) == 1 && seq_write_key_to_pointers_mapping[key].num_pointers > 0) {
		Block_manager_parent::register_write_outcome(event, status);
		page_hotness_measurer.register_event(event);

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

		Address selected_pointer = seq_write_key_to_pointers_mapping[key].pointers[i][j];
		selected_pointer.page++;
		seq_write_key_to_pointers_mapping[key].pointers[i][j] = selected_pointer;

		if (selected_pointer.page == BLOCK_SIZE) {
			Address free_block;
			if (parallel_degree == ONE) {
				free_block = find_free_unused_block(event.get_current_time());
				if (free_block.valid == NONE) {
					perform_gc(event.get_current_time());
				}
			} else if (parallel_degree == CHANNEL) {
				free_block = find_free_unused_block(event.get_address().package, event.get_current_time());
				if (free_block.valid == NONE) {
					perform_gc(event.get_address().package, event.get_current_time());
				}
			} else if (parallel_degree == LUN) {
				free_block = find_free_unused_block(event.get_address().package, event.get_address().die, event.get_current_time());
				if (free_block.valid == NONE) {
					perform_gc(event.get_address().package, event.get_address().die, event.get_current_time());
				}
			}
			if (free_block.valid != NONE) {
				seq_write_key_to_pointers_mapping[key].pointers[i][j] = free_block;
			} else {
				seq_write_key_to_pointers_mapping[key].num_pointers--;
			}
		}

	} else {
		Block_manager_parallel_wearwolf::register_write_outcome(event, status);
	}
	if (event.is_original_application_io()) {
		recorder->register_event(lb, event.get_current_time());
	}
}

void Block_manager_parallel_wearwolf_locality::sequential_event_metadata_removed(long key) {
	for (uint i = 0; i < PACKAGE_SIZE; i++) {
		for (uint j = 0; j < DIE_SIZE; j++) {
			Block_manager_parent::return_unfilled_block(seq_write_key_to_pointers_mapping[key].pointers[i][j]);
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

	map<long, sequential_writes_pointers >::iterator iter = seq_write_key_to_pointers_mapping.begin();
	for (; iter != seq_write_key_to_pointers_mapping.end(); iter++) {
		if (!has_free_pages((*iter).second.pointers[i][j])) {
			if (parallel_degree == ONE) {
				(*iter).second.pointers[i][j] = find_free_unused_block(event.get_current_time());
			} else if (parallel_degree == CHANNEL) {
				(*iter).second.pointers[i][j] = find_free_unused_block(i, event.get_current_time());
			} else if (parallel_degree == LUN) {
				(*iter).second.pointers[i][j] = find_free_unused_block(i, j, event.get_current_time());
			}
			if (has_free_pages((*iter).second.pointers[i][j])) {
				(*iter).second.num_pointers++;
			}
		}

	}
	//check_if_should_trigger_more_GC(event.get_current_time());
}

Block_manager_parallel_wearwolf_locality::sequential_writes_pointers::sequential_writes_pointers()
	: num_pointers(0),
	  pointers()
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

