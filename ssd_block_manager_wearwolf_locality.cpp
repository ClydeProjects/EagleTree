
#include "ssd.h"

using namespace ssd;
using namespace std;

#define THRESHOLD 3 // the number of sequential writes before we recognize the pattern as sequential

Block_manager_parallel_wearwolf_locality::Block_manager_parallel_wearwolf_locality(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel_wearwolf(ssd, ftl),
	  parallel_degree(LUN)
{
}

Block_manager_parallel_wearwolf_locality::~Block_manager_parallel_wearwolf_locality(void) {}

Block_manager_parallel_wearwolf_locality::sequential_writes_tracking::sequential_writes_tracking(double time)
	: counter(1),
	  last_LBA_timestamp(time),
	  pointers(NULL)
{}

Block_manager_parallel_wearwolf_locality::sequential_writes_tracking::~sequential_writes_tracking() {
}

pair<double, Address> Block_manager_parallel_wearwolf_locality::write(Event & event) {
	// check if write matches sequential pattern
	logical_address lb = event.get_logical_address();
	pair<double, Address> to_return;

	if (sequential_writes_key_lookup.count(lb) == 0) {
		to_return = Block_manager_parallel_wearwolf::write(event);
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swt = *sequential_writes_identification_and_data[key];
		if (swt.counter >= THRESHOLD) {
			to_return = perform_sequential_write(event, key, swt);
		} else if (swt.counter < THRESHOLD) {
			to_return = Block_manager_parallel_wearwolf::write(event);
		}
	}

	return to_return;
}

pair<double, Address> Block_manager_parallel_wearwolf_locality::perform_sequential_write(Event & event, logical_address key, sequential_writes_tracking & swt) {
	pair<double, Address> to_return;

	double cannot_be_scheduled_before_this_time = swt.last_LBA_timestamp;
	if (event.get_current_time() < cannot_be_scheduled_before_this_time) {
		to_return.first = cannot_be_scheduled_before_this_time - event.get_current_time();
		return to_return;
	}

	pair<bool, pair<uint, uint> > best_die_id = Block_manager_parent::get_free_die_with_shortest_IO_queue(swt.pointers);
	bool can_write = best_die_id.first;
	if (can_write) {
		to_return.second = swt.pointers[best_die_id.second.first][best_die_id.second.second];
		double time = in_how_long_can_this_event_be_scheduled(to_return.second, event.get_start_time() + event.get_time_taken());
		to_return.first = time;
	} else {
		to_return.first = 1;
	}
	return to_return;
}

// TODO: for the ONE and LUN degrees, try to do read-balancing
void Block_manager_parallel_wearwolf_locality::set_pointers_for_sequential_write(sequential_writes_tracking & swt, double time) {
	if (parallel_degree == ONE) {
		swt.pointers = vector<vector<Address> >(1, vector<Address>(1));
		Address free_block = find_free_unused_block(time);
		if (free_block.valid != NONE) {
			swt.pointers[0][0] = free_block;
		}
	} else if (parallel_degree == CHANNEL) {
		swt.pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(1));
		for (uint i = 0; i < SSD_SIZE; i++) {
			Address free_block = find_free_unused_block(i, time);
			if (free_block.valid != NONE) {
				swt.pointers[i][0] = free_block;
			}
		}
	} else if (parallel_degree == LUN) {
		swt.pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(PACKAGE_SIZE));
		for (uint i = 0; i < SSD_SIZE; i++) {
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				Address free_block = find_free_unused_block(i, j, time);
				if (free_block.valid != NONE) {
					swt.pointers[i][j] = free_block;
				}
			}
		}
	}
}

void Block_manager_parallel_wearwolf_locality::register_write_outcome(Event const& event, enum status status) {
	long lb = event.get_logical_address();
	if (sequential_writes_key_lookup.count(lb) == 0) {
		sequential_writes_key_lookup[lb + 1] = lb;
		sequential_writes_identification_and_data[lb] = new sequential_writes_tracking(event.get_current_time());

		Block_manager_parallel_wearwolf::register_write_outcome(event, status);
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		swm.counter++;
		swm.last_LBA_timestamp = event.get_start_time() + event.get_bus_wait_time();
		sequential_writes_key_lookup.erase(lb);
		sequential_writes_key_lookup[lb + 1] = key;

		if (swm.counter == THRESHOLD) {
			set_pointers_for_sequential_write(swm, event.get_current_time());
		}

		if (swm.counter <= THRESHOLD) {
			Block_manager_parallel_wearwolf::register_write_outcome(event, status);
		} else if (swm.counter > THRESHOLD) {
			Block_manager_parent::register_write_outcome(event, status);

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

			Address selected_pointer = swm.pointers[i][j];
			selected_pointer.page++;
			swm.pointers[i][j] = selected_pointer;

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
					swm.pointers[i][j] = free_block;
				}
			}

		}
	}
	remove_old_sequential_writes_metadata(event.get_current_time());
}

void Block_manager_parallel_wearwolf_locality::remove_old_sequential_writes_metadata(double time) {
	map<logical_address, sequential_writes_tracking*>::iterator iter = sequential_writes_identification_and_data.begin();
	while(iter != sequential_writes_identification_and_data.end())
	{
		if ((*iter).second->last_LBA_timestamp + 400 < time) {
			printf("deleting seq write with key %d:\n", (*iter).first);
			uint next_expected_lba = (*iter).second->counter + (*iter).first;
			sequential_writes_key_lookup.erase(next_expected_lba);
			sequential_writes_identification_and_data.erase(iter++);

		} else {
			++iter;
		}
	}

}


