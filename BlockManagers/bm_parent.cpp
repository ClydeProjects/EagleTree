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
using namespace std;

double Block_manager_parent::soonest_write_time = 0;

Block_manager_parent::Block_manager_parent(int num_age_classes)
 : ssd(NULL),
   ftl(NULL),
   free_block_pointers(SSD_SIZE, vector<Address>(PACKAGE_SIZE)),
   free_blocks(SSD_SIZE, vector<vector<deque<Address> > >(PACKAGE_SIZE, vector<deque<Address> >(num_age_classes, deque<Address>(0)) )),
   all_blocks(0),
   num_age_classes(num_age_classes),
   num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   IO_has_completed_since_last_shortest_queue_search(true),
   erase_queue(SSD_SIZE, queue< Event*>()),
   num_erases_scheduled_per_package(SSD_SIZE, 0),
   scheduler(NULL),
   wl(NULL),
   gc(NULL),
   migrator(NULL)
{}

Block_manager_parent::~Block_manager_parent() {
}

void Block_manager_parent::init(Ssd* new_ssd, FtlParent* new_ftl, IOScheduler* new_sched, Garbage_Collector* new_gc, Wear_Leveling_Strategy* new_wl, Migrator* new_migrator) {
	ssd = new_ssd;
	ftl = new_ftl;
	scheduler = new_sched;

	for (uint i = 0; i < SSD_SIZE; i++) {
		Package* package = ssd->get_package(i);
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Die* die = package->get_die(j);
			for (uint t = 0; t < DIE_SIZE; t++) {
				Plane* plane = die->get_plane(t);
				for (uint b = 0; b < PLANE_SIZE; b++) {
					Block* block = plane->get_block(b);
					free_blocks[i][j][0].push_back(Address(block->get_physical_address(), PAGE));
					all_blocks.push_back(block);
				}
			}
			Address pointer = free_blocks[i][j][0].back();
			free_block_pointers[i][j] = pointer;
			Free_Space_Per_LUN_Meter::mark_new_space(pointer, 0);
			free_blocks[i][j][0].pop_back();
		}
	}
	wl = new_wl;
	gc = new_gc;
	migrator = new_migrator;
}

Address Block_manager_parent::choose_copbyback_address(Event const& write) {
	Address ra = write.get_replace_address();
	if (!has_free_pages(free_block_pointers[ra.package][ra.die])) {
		Address new_block = find_free_unused_block(ra.package, ra.die, write.get_current_time());
		if (has_free_pages(new_block)) {
			free_block_pointers[ra.package][ra.die] = new_block;
			Free_Space_Per_LUN_Meter::mark_new_space(new_block, write.get_current_time());
		}
	}
	return has_free_pages(free_block_pointers[ra.package][ra.die]) ? free_block_pointers[ra.package][ra.die] : Address();
}

Address Block_manager_parent::choose_flexible_read_address(Flexible_Read_Event* fr) {
	vector<vector<Address> > candidates = fr->get_candidates();
	pair<bool, pair<int, int> > result = get_free_block_pointer_with_shortest_IO_queue(candidates);
	if (!result.first) {
		return Address();
	}
	pair<int, int> coor = result.second;
	return candidates[coor.first][coor.second];
}

Address Block_manager_parent::choose_write_address(Event& write) {
	//printf("num_available_pages_for_new_writes   %d\n", num_available_pages_for_new_writes);
	bool can_write = num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();

	if (!can_write) {
		return Address();
	}

	if (write.get_event_type() == COPY_BACK) {
		return choose_copbyback_address(write);
	}

	Address a = choose_best_address(write);
	if (has_free_pages(a)) {
		return a;
	}

	if (GREED_SCALE > 0 && !write.is_garbage_collection_op() && migrator->how_many_gc_operations_are_scheduled() == 0) {
		migrator->schedule_gc(write.get_current_time(), -1, -1, -1 ,-1);
	}
	if (write.is_garbage_collection_op() || migrator->how_many_gc_operations_are_scheduled() == 0) {
		a = choose_any_address(write);
		if (has_free_pages(a)) {
			return a;
		}
	}
	return Address();
}

void Block_manager_parent::register_erase_outcome(Event& event, enum status status) {
	IO_has_completed_since_last_shortest_queue_search = true;

	Address a = event.get_address();
	a.valid = PAGE;
	a.page = 0;
	//Block* b = &ssd->get_package()[a.package]->get_die()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	//StatisticsGatherer::get_global_instance()->print();

	wl->register_erase_completion(event);

	if (USE_ERASE_QUEUE) {
		assert(num_erases_scheduled_per_package[a.package] == 1);
		num_erases_scheduled_per_package[a.package]--;
		assert(num_erases_scheduled_per_package[a.package] == 0);

		if (erase_queue[a.package].size() > 0) {
			Event* new_erase = erase_queue[a.package].front();
			double diff = event.get_current_time() - new_erase->get_current_time();
			new_erase->incr_bus_wait_time(diff);
			erase_queue[a.package].pop();
			num_erases_scheduled_per_package[a.package]++;
			scheduler->schedule_event(new_erase);
		}
	}

	uint age_class = sort_into_age_class(a);
	free_blocks[a.package][a.die][age_class].push_back(a);

	num_free_pages += BLOCK_SIZE;
	num_available_pages_for_new_writes += BLOCK_SIZE;
	//printf("%d   %d\n", num_available_pages_for_new_writes, num_free_pages);
	Free_Space_Meter::register_num_free_pages_for_app_writes(num_available_pages_for_new_writes, event.get_current_time());

	if (migrator->how_many_gc_operations_are_scheduled() == 0) {
		assert(num_free_pages == num_available_pages_for_new_writes);
	}

	check_if_should_trigger_more_GC(event);

}

void Block_manager_parent::register_write_arrival(Event const& write) {

}

void Block_manager_parent::register_register_cleared() {
	IO_has_completed_since_last_shortest_queue_search = true;
}

uint Block_manager_parent::sort_into_age_class(Address const& a) const {
	Block* b = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	uint age = b->get_age();
	double normalized_age = wl->get_normalised_age(age);
	int klass = floor(normalized_age * num_age_classes * 0.99999);
	return klass;
}

void Block_manager_parent::increment_pointer(Address& pointer) {
	Address p = pointer;
	p.page = p.page + 1;
	pointer = p;
}

void Block_manager_parent::register_write_outcome(Event const& event, enum status status) {
	IO_has_completed_since_last_shortest_queue_search = true;

	assert(num_free_pages > 0);
	num_free_pages--;

	//printf("gc going on   %d\n", how_many_gc_operations_are_scheduled());

	if (!event.is_garbage_collection_op()) {
		assert(num_available_pages_for_new_writes > 0);
		num_available_pages_for_new_writes--;
		Free_Space_Meter::register_num_free_pages_for_app_writes(num_available_pages_for_new_writes, event.get_current_time());
	}
	//printf("%d   %d\n", num_available_pages_for_new_writes, num_free_pages);
	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE && migrator->how_many_gc_operations_are_scheduled() == 0) {
		migrator->schedule_gc(event.get_current_time(), -1, -1, -1, -1);
	}

	Address ba = event.get_address();
	if (ba.compare(free_block_pointers[ba.package][ba.die]) >= BLOCK) {
		increment_pointer(free_block_pointers[ba.package][ba.die]);
		if (!has_free_pages(free_block_pointers[ba.package][ba.die])) {
			if (PRINT_LEVEL > 1) {
				printf("hot pointer "); free_block_pointers[ba.package][ba.die].print(); printf(" is out of space");
			}
			Address free_pointer = find_free_unused_block(ba.package, ba.die, YOUNG, event.get_current_time());
			if (has_free_pages(free_pointer)) {
				free_block_pointers[ba.package][ba.die] = free_pointer;
			}
			if (PRINT_LEVEL > 1) {
				if (free_pointer.valid == NONE) printf(", and a new unused block could not be found.\n");
				else printf(".\n");
			}
		}
	}

	if (!has_free_pages(free_block_pointers[ba.package][ba.die])) {
		Free_Space_Per_LUN_Meter::mark_out_of_space(ba, event.get_current_time());
	}
	else {
		Free_Space_Per_LUN_Meter::mark_new_space(ba, event.get_current_time());
	}
}

void Block_manager_parent::trim(Event const& event) {
	IO_has_completed_since_last_shortest_queue_search = true;
}

int Block_manager_parent::get_num_pointers_with_free_space() const {
	int sum = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			if (has_free_pages(free_block_pointers[i][j])) {
				sum++;
			}
		}
	}
	return sum;
}

int Block_manager_parent::get_num_free_blocks() const {
	int sum = 0;
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			sum += get_num_free_blocks(i, j);
		}
	}
	return sum;
}

void Block_manager_parent::print_free_blocks() const {
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			for (int k = 0; k < num_age_classes; k++) {
				for (auto& q : free_blocks[i][j][k]) {
					q.print();
					printf("\n");
				}
			}
		}
	}
}

int Block_manager_parent::get_num_free_blocks(int package, int die) const {
	int num_free_blocks = 0;
	for (int i = 0; i < num_age_classes; i++) {
		num_free_blocks += free_blocks[package][die][i].size();
	}
	return num_free_blocks;
}

void Block_manager_parent::register_read_command_outcome(Event const& event, enum status status) {
	IO_has_completed_since_last_shortest_queue_search = true;
	assert(event.get_event_type() == READ_COMMAND);
}

void Block_manager_parent::register_read_transfer_outcome(Event const& event, enum status status) {
	IO_has_completed_since_last_shortest_queue_search = true;
	migrator->register_ECC_check_on(event.get_logical_address()); // An ECC check happens in a normal read-write GC operation
	assert(event.get_event_type() == READ_TRANSFER);
}

bool Block_manager_parent::can_write(Event const& write) const {
	return num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();
}

void Block_manager_parent::check_if_should_trigger_more_GC(Event const& event) {
	if (num_free_pages <= BLOCK_SIZE) {
		migrator->schedule_gc(event.get_current_time(), -1, -1, -1, -1);
	}

	int num_luns_with_space = SSD_SIZE * PACKAGE_SIZE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(free_block_pointers[i][j]) || get_num_free_blocks(i, j) < GREED_SCALE) {
				migrator->schedule_gc(event.get_current_time(), i, j, -1, -1);
			}
			if (!has_free_pages(free_block_pointers[i][j])) {
				num_luns_with_space--;
			}
		}
	}
	//printf("num_luns_with_space:  %d\n", num_luns_with_space);
}

// This function takes a vector of channels, each of each has a vector of dies
// it finds the die with the shortest queue, and returns its ID
// if all dies are busy, the boolean field is returned as false
pair<bool, pair<int, int> > Block_manager_parent::get_free_block_pointer_with_shortest_IO_queue(vector<vector<Address> > const& dies) const {
	uint best_channel_id = UNDEFINED;
	uint best_die_id = UNDEFINED;
	bool can_write = false;
	bool at_least_one_address = false;
	double shortest_time = std::numeric_limits<double>::max( );
	//for (uint i = 0; i < dies.size(); i++) {
	for (int i = dies.size() - 1; i >= 0; i--) {
		double earliest_die_finish_time = std::numeric_limits<double>::max();
		uint die_with_earliest_finish_time = 0;
		for (uint j = 0; j < dies[i].size(); j++) {
		//for (int j = dies[i].size() - 1; j >= 0; j--) {
			Address pointer = dies[i][j];
			at_least_one_address = at_least_one_address ? at_least_one_address : pointer.valid == PAGE;
			bool die_has_free_pages = has_free_pages(pointer);
			uint channel_id = pointer.package;
			uint die_id = pointer.die;
			bool die_register_is_busy = ssd->get_package(channel_id)->get_die(die_id)->register_is_busy();
			if (die_has_free_pages && !die_register_is_busy) {
				can_write = true;
				double channel_finish_time = ssd->get_currently_executing_operation_finish_time(channel_id);
				double die_finish_time = ssd->get_package(channel_id)->get_die(die_id)->get_currently_executing_io_finish_time();
				double max = std::max(channel_finish_time,die_finish_time);

				if (die_finish_time < earliest_die_finish_time) {
					earliest_die_finish_time = die_finish_time;
					die_with_earliest_finish_time = j;
				}

				if (max < shortest_time || (max == shortest_time && die_with_earliest_finish_time == j)) {
					best_channel_id = i;
					best_die_id = j;
					shortest_time = max;
				}
			}
		}
	}
	//assert(at_least_one_address);
	return pair<bool, pair<int, int> >(can_write, pair<int, int>(best_channel_id, best_die_id));
}

Address Block_manager_parent::get_free_block_pointer_with_shortest_IO_queue() {
	pair<bool, pair<int, int> > best_die;
	if (IO_has_completed_since_last_shortest_queue_search) {
	    best_die = get_free_block_pointer_with_shortest_IO_queue(free_block_pointers);
		last_get_free_block_pointer_with_shortest_IO_queue_result = best_die;
		IO_has_completed_since_last_shortest_queue_search = false;
	} else {
		best_die = last_get_free_block_pointer_with_shortest_IO_queue_result;
	}
	if (!best_die.first) {
		return Address();
	} else {
		return free_block_pointers[best_die.second.first][best_die.second.second];
	}
}

bool Block_manager_parent::Copy_backs_in_progress(Address const& addr) {
	return false;
}

// gives time until both the channel and die are clear
double Block_manager_parent::in_how_long_can_this_event_be_scheduled(Address const& address, double event_time, event_type type) const {
	if (address.valid == NONE) {
		return BUS_DATA_DELAY + BUS_CTRL_DELAY;
	}

	uint package_id = address.package;
	uint die_id = address.die;
	double channel_finish_time = ssd->get_currently_executing_operation_finish_time(package_id);
	double die_finish_time = ssd->get_package(package_id)->get_die(die_id)->get_currently_executing_io_finish_time();
	double max_time = max(channel_finish_time, die_finish_time);
	double time = fmax(0.0, max_time - event_time);
	if (type == WRITE) {
		time = fmin(time, BUS_DATA_DELAY + BUS_CTRL_DELAY);
		return time; // in_how_long_can_this_write_be_scheduled(event_time);
	}
	/*if (alternative_time > time) {
		int i = 0;
		i++;
	}*/
	return time;
}

double Block_manager_parent::in_how_long_can_this_write_be_scheduled(double current_time) const {
	double min_execution_time = INFINITE;
	for (int i = 0; i < SSD_SIZE; i++) {
		double channel_finish_time = ssd->get_currently_executing_operation_finish_time(i);
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			bool busy = ssd->get_package(i)->get_die(j)->register_is_busy();
			double die_finish_time = ssd->get_package(i)->get_die(j)->get_currently_executing_io_finish_time();
			double max_time = fmax(channel_finish_time, die_finish_time);
			max_time += busy ? BUS_DATA_DELAY + BUS_CTRL_DELAY : 0;
			min_execution_time = fmin(min_execution_time, max_time);
		}
	}
	return fmax(min_execution_time - current_time, 0.0);
}

double Block_manager_parent::in_how_long_can_this_write_be_scheduled2(double current_time) const {
	return fmax(0.0, soonest_write_time - current_time);
}

void Block_manager_parent::update_next_possible_write_time() const {
	double min_execution_time = INFINITE;
	for (int i = 0; i < SSD_SIZE; i++) {
		double channel_finish_time = ssd->get_currently_executing_operation_finish_time(i);
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			bool busy = ssd->get_package(i)->get_die(j)->register_is_busy();
			double die_finish_time = ssd->get_package(i)->get_die(j)->get_currently_executing_io_finish_time();
			double max_time = fmax(channel_finish_time, die_finish_time);
			max_time += busy ? BUS_DATA_DELAY + BUS_CTRL_DELAY : 0;
			min_execution_time = fmin(min_execution_time, max_time);
		}
	}
	soonest_write_time = min_execution_time;
}

bool Block_manager_parent::can_schedule_on_die(Address const& address, event_type type, uint app_io_id) const {
	uint package_id = address.package;
	uint die_id = address.die;
	bool busy = ssd->get_package(package_id)->get_die(die_id)->register_is_busy();
	if (!busy) {
		return true;
	}
	uint application_io = ssd->get_package(package_id)->get_die(die_id)->get_last_read_application_io();
	return (type == READ_TRANSFER || type == COPY_BACK ) && application_io == app_io_id;
}

bool Block_manager_parent::is_die_register_busy(Address const& addr) const {
	uint package_id = addr.package;
	uint die_id = addr.die;
	return ssd->get_package(package_id)->get_die(die_id)->register_is_busy();
}

bool Block_manager_parent::can_schedule_write_immediately(Address const& prospective_dest, double current_time) {
	bool free_pages = has_free_pages(prospective_dest);
	bool die_not_busy =		!is_die_register_busy(prospective_dest);
	bool no_copy_back =		!Copy_backs_in_progress(prospective_dest);
	bool can_be_scheduled_now =	in_how_long_can_this_write_be_scheduled(current_time) == 0;
	return free_pages && die_not_busy && no_copy_back && can_be_scheduled_now;
}

void Block_manager_parent::register_trim_making_gc_redundant(Event* trim) {
	if (PRINT_LEVEL > 1) {
		printf("a trim made a gc event redundant\n");
		//printf("%d   %d\n", num_available_pages_for_new_writes, num_free_pages);
	}
	num_available_pages_for_new_writes++;
	Free_Space_Meter::register_num_free_pages_for_app_writes(num_available_pages_for_new_writes, trim->get_current_time());
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parent::find_free_unused_block(double time) {
	vector<int> order = Random_Order_Iterator::get_iterator(SSD_SIZE);
	while (order.size() > 0) {
		int index = order.back();
		order.pop_back();
		Address address = find_free_unused_block(index, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, double time) {
	assert(package_id < SSD_SIZE);
	vector<int> order = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
	while (order.size() > 0) {
		int index = order.back();
		order.pop_back();
		Address address = find_free_unused_block(package_id, index, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

// finds and returns a free block from a particular die in the SSD
Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id, double time) {
	assert(package_id < SSD_SIZE && die_id < PACKAGE_SIZE);
	vector<int> order = Random_Order_Iterator::get_iterator(num_age_classes);
	while (order.size() > 0) {
		int index = order.back();
		order.pop_back();
		Address address = find_free_unused_block(package_id, die_id, index, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id, uint klass, double time) {
	assert(package_id < SSD_SIZE && die_id < PACKAGE_SIZE && klass < num_age_classes);
	Address to_return;
	uint num_free_blocks_left = free_blocks[package_id][die_id][klass].size();
	if (num_free_blocks_left > 0) {
		to_return = free_blocks[package_id][die_id][klass].back();
		free_blocks[package_id][die_id][klass].pop_back();
		num_free_blocks_left--;
		assert(has_free_pages(to_return));
	}
	if (to_return.valid != NONE &&  num_free_blocks_left == 0) {
		//StateVisualiser::print_page_status();
	}
	if (num_free_blocks_left < GREED_SCALE) {
		migrator->schedule_gc(time, package_id, die_id, -1, -1);
	}
	return to_return;
}

Address Block_manager_parent::find_free_unused_block(uint package, uint die, enum age age, double time) {
	if (age == YOUNG) {
		for (int i = 0; i < num_age_classes; i++) {
			Address block = find_free_unused_block(package, die, i, time);
			if (has_free_pages(block)) {
				return block;
			}
		}
	} else if (age == OLD) {
		for (int i = num_age_classes - 1; i >= 0; i--) {
			Address block = find_free_unused_block(package, die, i, time);
			if (has_free_pages(block)) {
				return block;
			}
		}
	}
	return Address();
}

Address Block_manager_parent::find_free_unused_block(enum age age, double time) {
	vector<int> order1 = Random_Order_Iterator::get_iterator(SSD_SIZE);
	for (uint i = 0; i < SSD_SIZE; i++) {
		int package = order1[i];
		vector<int> order2 = Random_Order_Iterator::get_iterator(PACKAGE_SIZE);
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			int die = order2[j];
			Address block = find_free_unused_block(package, die, age, time);
			if (has_free_pages(block)) {
				return block;
			}
		}
	}
	return Address();
}

void Block_manager_parent::return_unfilled_block(Address pba, double current_time, bool give_to_block_pointers) {
	if (has_free_pages(pba)) {
		int age_class = sort_into_age_class(pba);
		if (!give_to_block_pointers || has_free_pages(free_block_pointers[pba.package][pba.die])) {
			free_blocks[pba.package][pba.die][age_class].push_back(pba);
		} else {
			free_block_pointers[pba.package][pba.die] = pba;
			Free_Space_Per_LUN_Meter::mark_new_space(pba, current_time);
		}
	}
}

void Block_manager_parent::copy_state(Block_manager_parent* bm) {
	free_block_pointers = bm->free_block_pointers;
	free_blocks = bm->free_blocks;
	all_blocks = bm->all_blocks;
	num_age_classes = bm->num_age_classes;
	num_free_pages = bm->num_free_pages;
	num_available_pages_for_new_writes = bm->num_available_pages_for_new_writes;
	erase_queue = bm->erase_queue;
	num_erases_scheduled_per_package = bm->num_erases_scheduled_per_package;
	ssd = bm->ssd;
	ftl = bm->ftl;
	scheduler = bm->scheduler;
	wl = bm->wl;
	gc = bm->gc;
	migrator = bm->migrator;
}

Block_manager_parent* Block_manager_parent::get_new_instance() {
	Block_manager_parent* bm;
	switch ( BLOCK_MANAGER_ID ) {
		case 0: bm = new Block_manager_parallel(); break;
		case 1: bm = new Shortest_Queue_Hot_Cold_BM(); break;
		case 2: bm = new Sequential_Locality_BM(); break;
		case 3: bm = new Block_manager_roundrobin(); break;
		case 5: bm = new Block_Manager_Tag_Groups(); break;
		case 6: bm = new Block_Manager_Groups(); break;
		case 7: bm = new bm_gc_locality(); break;
		default: bm = new Block_manager_parallel(); break;
	}
	return bm;
}

pointers::pointers(Block_manager_parent* bm) : bm(bm), blocks(SSD_SIZE, vector<Address>(PACKAGE_SIZE, Address())) {
	for (int i = 0; i < SSD_SIZE; i++) {
		for (int j = 0; j < PACKAGE_SIZE; j++) {
			blocks[i][j] = bm->find_free_unused_block(i, j, 0);
		}
	}
}

pointers::pointers() : bm(NULL), blocks(SSD_SIZE, vector<Address>(PACKAGE_SIZE, Address())) {
}


void pointers::register_completion(Event const& e) {
	blocks[e.get_address().package][e.get_address().die].page++;
}
Address pointers::get_best_block(Block_manager_parent* bm) const {
	pair<bool, pair<int, int> > result = bm->get_free_block_pointer_with_shortest_IO_queue(blocks);
	if (result.first) {
		return blocks[result.second.first][result.second.second];
	}
	return Address();
}

void pointers::print() const {
	for (int i = 0; i < blocks.size(); i++) {
		for (int j = 0; j < blocks[i].size(); j++) {
			printf("%d %d %d\n", i, j, blocks[i][j].page);
		}
	}
}

int pointers::get_num_free_blocks() const {
	int num_free_blocks = 0;
	for (int i = 0; i < blocks.size(); i++) {
		for (int j = 0; j < blocks[i].size(); j++) {
			if (blocks[i][j].page < BLOCK_SIZE && blocks[i][j].valid == PAGE) {
				num_free_blocks++;
			}
		}
	}
	return num_free_blocks;
}

void pointers::retire(double current_time) {
	int num_free_blocks = 0;
	for (int i = 0; i < blocks.size(); i++) {
		for (int j = 0; j < blocks[i].size(); j++) {
			//bm->free_blocks[i][j].push_back(blocks[i][j]);
			bm->return_unfilled_block(blocks[i][j], current_time, false);
		}
	}
}

