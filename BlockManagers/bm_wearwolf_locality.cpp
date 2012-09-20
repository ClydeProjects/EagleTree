
#include "../ssd.h"

using namespace ssd;
using namespace std;

Wearwolf_Locality::Wearwolf_Locality(Ssd& ssd, FtlParent& ftl)
	: Wearwolf(ssd, ftl),
	  parallel_degree(LUN),
	  seq_write_key_to_pointers_mapping(),
	  detector(new Sequential_Pattern_Detector(WEARWOLF_LOCALITY_THRESHOLD)),
	  strat(ROUND_ROBIN),
	  random_number_generator(1111)
{
	parallel_degree = LOCALITY_PARALLEL_DEGREE == 0 ? ONE : LOCALITY_PARALLEL_DEGREE == 1 ? CHANNEL : LUN;

	detector->set_listener(this);
}

Wearwolf_Locality::~Wearwolf_Locality() {
	delete detector;
}

void Wearwolf_Locality::register_write_arrival(Event const& event) {
	if (!event.is_original_application_io()) {
		return;
	}

	if (PRINT_LEVEL > 1) {
		printf("arrival: %d  in time: %f\n", event.get_logical_address(), event.get_current_time());
	}

	ulong lb = event.get_logical_address();
	int tag = event.get_tag();
	if (tag != -1 && tag_map.count(tag) == 0) {
		tagged_sequential_write tsw(lb, event.get_size());
		tag_map[tag] = tsw;
		if (PRINT_LEVEL > 1) {
			printf("TAG SEEN FOR FIRST TIME!  KEY: %d   TAG: %d  SIZE: %d  \n", lb, tag, event.get_size());
		}
		set_pointers_for_tagged_sequential_write(tag, event.get_current_time());
		return;
	}
	else if (tag != -1 && tag_map.count(tag) == 1) {
		return;
	}

	sequential_writes_tracking const& swt = detector->register_event(lb, event.get_current_time());
	// checks if should allocate pointers for the pattern
	if (swt.num_times_pattern_has_repeated == 0 && swt.counter == WEARWOLF_LOCALITY_THRESHOLD) {
		if (PRINT_LEVEL > 1) {
			printf("SEQUENTIAL PATTERN IDENTIFIED!  KEY: %d \n", swt.key);
		}
		set_pointers_for_sequential_write(swt.key, event.get_current_time());
	}
	if (swt.num_times_pattern_has_repeated > 0 || swt.counter >= WEARWOLF_LOCALITY_THRESHOLD) {
		arrived_writes_to_sequential_key_mapping[event.get_id()] = swt.key;
	}
}


Address Wearwolf_Locality::choose_best_address(Event const& event) {

	if (event.is_garbage_collection_op()) {
		return Wearwolf::choose_best_address(event);
	}

	int tag = event.get_tag();
	if (tag != UNDEFINED && tag_map.count(tag) == 1 && seq_write_key_to_pointers_mapping[tag_map[tag].key].num_pointers > 0) {
		return perform_sequential_write(event, tag_map[tag].key);
	}

	detector->remove_old_sequential_writes_metadata(event.get_start_time());

	if (arrived_writes_to_sequential_key_mapping.count(event.get_id()) == 0) {
		return Wearwolf::choose_best_address(event);
	}

	int key = arrived_writes_to_sequential_key_mapping[event.get_id()];

	if (seq_write_key_to_pointers_mapping.count(key) == 0) {
		arrived_writes_to_sequential_key_mapping.erase(event.get_id());
		return Wearwolf::choose_best_address(event);
	}

	if (seq_write_key_to_pointers_mapping[key].num_pointers == 0) {
		return Wearwolf::choose_best_address(event);
	}

	return perform_sequential_write(event, key);
}

Address Wearwolf_Locality::choose_any_address() {
	Address a = Wearwolf::choose_any_address();
	if (has_free_pages(a)) {
		return a;
	}
	map<long, sequential_writes_pointers >::iterator iter = seq_write_key_to_pointers_mapping.begin();
	for (; iter != seq_write_key_to_pointers_mapping.end(); iter++) {
		vector<vector<Address> >& pointers = (*iter).second.pointers;
		for (uint i = 0; i < pointers.size(); i++) {
			for (uint j = 0; j < pointers.size(); j++) {
				if (has_free_pages(pointers[i][j])) {
					return pointers[i][j];
				}
			}
		}
	}
	return Address();
}

Address Wearwolf_Locality::perform_sequential_write(Event const& event, long key) {
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
		//schedule_gc(event.get_current_time(), -1, -1, -1);  // TODO only trigger GC if tagged need more space
		to_return = Wearwolf::choose_best_address(event);
	}
	return to_return;
}

Address Wearwolf_Locality::perform_sequential_write_shortest_queue(sequential_writes_pointers& swp) {
	pair<bool, pair<uint, uint> > best_die_id = get_free_block_pointer_with_shortest_IO_queue(swp.pointers);
	bool can_write = best_die_id.first;
	if (can_write) {
		Address chosen = swp.pointers[best_die_id.second.first][best_die_id.second.second];
		return swp.pointers[best_die_id.second.first][best_die_id.second.second];
	}
	return Address();
}

Address Wearwolf_Locality::perform_sequential_write_round_robin(sequential_writes_pointers& swp) {
	uint cursor = swp.cursor;
	vector<vector<Address> >& p = swp.pointers;
	uint package = cursor % p.size();
	uint die = (swp.cursor / swp.pointers.size()) % swp.pointers[package].size();
	return swp.pointers[package][die];
}


// TODO: for the ONE and LUN degrees, try to do read-balancing
void Wearwolf_Locality::set_pointers_for_sequential_write(long key, double time) {
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

void Wearwolf_Locality::set_pointers_for_tagged_sequential_write(int tag, double time) {
	int num_blocks_needed = ceil(tag_map[tag].size / (double)BLOCK_SIZE);
	int num_LUNs = SSD_SIZE * PACKAGE_SIZE;
	int num_blocks_to_allocate_now = min(num_blocks_needed, num_LUNs);
	int key = tag_map[tag].key;
	seq_write_key_to_pointers_mapping[key].tag = tag;
	vector<vector<Address> >& pointers = seq_write_key_to_pointers_mapping[key].pointers = vector<vector<Address> >(SSD_SIZE, vector<Address>(PACKAGE_SIZE));
	int random_offset = random_number_generator();
	for (int i = 0 ; i < num_blocks_to_allocate_now; i++) {
		int random_index = random_offset + i;
		uint package = random_index % SSD_SIZE;
		uint die = (random_index / SSD_SIZE) % PACKAGE_SIZE;
		Address free_block = find_free_unused_block(package, die, time);
		if (has_free_pages(free_block)) {
			tag_map[tag].free_allocated_space += BLOCK_SIZE - free_block.page;
			seq_write_key_to_pointers_mapping[key].num_pointers++;
			assert(package == free_block.package);
			assert(die == free_block.die);
			pointers[package][die] = free_block;
		}
	}
}

// must handle situation in which a pointer was chosen for GC operation from choose_any_location
void Wearwolf_Locality::process_write_completion(Event const& event, long key, pair<long, long> index) {

	Block_manager_parent::register_write_outcome(event, SUCCESS);
	page_hotness_measurer.register_event(event);

	sequential_writes_pointers& swp = seq_write_key_to_pointers_mapping[key];

	Address selected_pointer = swp.pointers[index.first][index.second];
	assert(selected_pointer.valid == PAGE);
	selected_pointer.page++;
	swp.pointers[index.first][index.second] = selected_pointer;

	if (strat == ROUND_ROBIN) {
		swp.cursor++;
	}

	bool allocate_more_blocks = !has_free_pages(selected_pointer);

	if (allocate_more_blocks && event.get_tag() != UNDEFINED && !tag_map[event.get_tag()].need_more_space()) {
		allocate_more_blocks = false;
	}

	if (allocate_more_blocks) {
		Address free_block;
		if (parallel_degree == LUN || event.get_tag() != UNDEFINED) {
			free_block = find_free_unused_block(event.get_address().package, event.get_address().die, event.get_current_time());
			if (free_block.valid == NONE) {
				//schedule_gc(event.get_current_time(), event.get_address().package, event.get_address().die);
			}
		}
		else if (parallel_degree == CHANNEL) {
			free_block = find_free_unused_block(event.get_address().package, event.get_current_time());
			if (free_block.valid == NONE) {
				//schedule_gc(event.get_current_time(), event.get_address().package);
			}
		}
		else if (parallel_degree == ONE) {
			free_block = find_free_unused_block(event.get_current_time());
			if (free_block.valid == NONE) {
				//schedule_gc(event.get_current_time());
			}
		}
		if (has_free_pages(free_block)) {
			swp.pointers[index.first][index.second] = free_block;
		} else {
			swp.num_pointers--;
		}
	}
}

void Wearwolf_Locality::print_tags() {
	map<long, sequential_writes_pointers> a;
	map<long, tagged_sequential_write>::iterator iter = tag_map.begin();
	for (; iter != tag_map.end(); iter++) {
		tagged_sequential_write& f = (*iter).second;
		sequential_writes_pointers& d = seq_write_key_to_pointers_mapping[f.key];
		printf("  %d  %d  %d   %d   %d   %d  %d\n", (*iter).first, f.key, f.size, f.free_allocated_space, f.num_written, d.num_pointers, d.tag);
		for (uint i = 0; i < d.pointers.size(); i++) {
			for (uint j = 0; j < d.pointers[i].size(); j++) {
				if (has_free_pages(d.pointers[i][j])) {
					printf("    "); d.pointers[i][j].print(); printf("\n");
				}
			}
		}
	}
}

void Wearwolf_Locality::register_write_outcome(Event const& event, enum status status) {
	int tag = event.get_tag();
	if (tag != UNDEFINED) {
		tag_map[tag].num_written++;
	}

	map<long, sequential_writes_pointers >::iterator iter = seq_write_key_to_pointers_mapping.begin();
	bool found = false;
	for (; iter != seq_write_key_to_pointers_mapping.end(); iter++) {
		vector<vector<Address> >& pointers = (*iter).second.pointers;
		for (uint i = 0; i < pointers.size(); i++) {
			for (uint j = 0; j < pointers[i].size(); j++) {
				if (has_free_pages(pointers[i][j]) && event.get_address().compare(pointers[i][j]) >= BLOCK) {
					long key = (*iter).first;
					pair<long, long> index(i, j);
					process_write_completion(event, key, index);
					found = true;
				}
			}
		}
	}

	if (tag != UNDEFINED && tag_map[tag].is_finished()) {
		sequential_event_metadata_removed(tag_map[tag].key);
		tag_map.erase(tag);
	}

	if (!found) {
		Wearwolf::register_write_outcome(event, status);
	}
}

void Wearwolf_Locality::sequential_event_metadata_removed(long key) {
	if (seq_write_key_to_pointers_mapping.count(key) == 0) {
		return;
	}
	sequential_writes_pointers& a = seq_write_key_to_pointers_mapping[key];
	//printf("Returning key %d: \n", key );
	for (uint i = 0; i < a.pointers.size(); i++) {
		for (uint j = 0; j < a.pointers[i].size(); j++) {
			Address& pointer = a.pointers[i][j];
			//printf("  "); pointer.print(); printf("\n");
			Block_manager_parent::return_unfilled_block(pointer);
		}
	}
	seq_write_key_to_pointers_mapping.erase(key);
}

void Wearwolf_Locality::register_erase_outcome(Event const& event, enum status status) {
	Wearwolf::register_erase_outcome(event, status);
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
		sequential_writes_pointers& swt = (*iter).second;
		if (swt.tag != -1 && !tag_map[swt.tag].need_more_space()) {
			continue;
		}
		Address& pointer = swt.pointers[i][j];
		if (!has_free_pages(pointer)) {
			Address new_block;
			if (parallel_degree == LUN || swt.tag != UNDEFINED) {
				new_block = find_free_unused_block(i, j, event.get_current_time());
			}
			else if (parallel_degree == CHANNEL) {
				new_block = find_free_unused_block(i, event.get_current_time());
			}
			else if (parallel_degree == ONE) {
				new_block = find_free_unused_block(event.get_current_time());
			}
			if (has_free_pages(new_block)) {
				swt.num_pointers++;
				swt.pointers[i][j] = new_block;
			}
		}
	}
}

Wearwolf_Locality::sequential_writes_pointers::sequential_writes_pointers()
	: num_pointers(0),
	  pointers(),
	  cursor(rand() % 100),
	  tag(-1)
{}

