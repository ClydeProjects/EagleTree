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

Block_manager_parent::Block_manager_parent(Ssd& ssd, FtlParent& ftl, int num_age_classes)
 : ssd(ssd),
   ftl(ftl),
   free_block_pointers(SSD_SIZE, vector<Address>(PACKAGE_SIZE)),
   free_blocks(SSD_SIZE, vector<vector<vector<Address> > >(PACKAGE_SIZE, vector<vector<Address> >(num_age_classes, vector<Address>(0)) )),
   all_blocks(0),
   max_age(1),
   num_age_classes(num_age_classes),
   blocks_with_min_age(),
   num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   blocks_being_garbage_collected(),
   gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(num_age_classes, set<long>()))),
   num_blocks_being_garbaged_collected_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
   order_randomiser(),
   IO_has_completed_since_last_shortest_queue_search(true),
   erase_queue(SSD_SIZE, queue< Event*>()),
   num_erases_scheduled_per_package(SSD_SIZE, 0),
   random_number_generator(90),
   num_erases_up_to_date(0)
{
	for (uint i = 0; i < SSD_SIZE; i++) {
		Package& package = ssd.getPackages()[i];
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Die& die = package.getDies()[j];
			for (uint t = 0; t < DIE_SIZE; t++) {
				Plane& plane = die.getPlanes()[t];
				for (uint b = 0; b < PLANE_SIZE; b++) {
					Block& block = plane.getBlocks()[b];
					free_blocks[i][j][0].push_back(Address(block.get_physical_address(), PAGE));
					all_blocks.push_back(&block);
					blocks_with_min_age.insert(&block);
				}
			}
			free_block_pointers[i][j] = free_blocks[i][j][0].back();
			free_blocks[i][j][0].pop_back();
		}
	}
}

Block_manager_parent::~Block_manager_parent() {}

Address Block_manager_parent::choose_copbyback_address(Event const& write) {
	Address ra = write.get_replace_address();
	if (!has_free_pages(free_block_pointers[ra.package][ra.die])) {
		Address new_block = find_free_unused_block(ra.package, ra.die, write.get_current_time());
		if (has_free_pages(new_block)) {
			free_block_pointers[ra.package][ra.die] = new_block;
		}
	}
	return has_free_pages(free_block_pointers[ra.package][ra.die]) ? free_block_pointers[ra.package][ra.die] : Address();
}

Address Block_manager_parent::choose_flexible_read_address(Event* read) {
	Flexible_Read_Event* fr = dynamic_cast<Flexible_Read_Event*>(read);
	vector<vector<Address> > candidates = fr->get_candidates();
	pair<bool, pair<int, int> > result = get_free_block_pointer_with_shortest_IO_queue(candidates);
	if (!result.first) {
		return Address();
	}
	pair<int, int> coor = result.second;
	return candidates[coor.first][coor.second];
}

Address Block_manager_parent::choose_address(Event const& write) {
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

	/*if (write.is_garbage_collection_op() && has_free_pages(a) && can_schedule_write_immediately(a, write.get_current_time())) {
		return a;
	}*/

	if (!write.is_garbage_collection_op() && how_many_gc_operations_are_scheduled() == 0) {
		schedule_gc(write.get_current_time(), -1, -1, -1 ,-1);
	}
	if (write.is_garbage_collection_op() || how_many_gc_operations_are_scheduled() == 0) {
		a = choose_any_address(write);
		if (has_free_pages(a)) {
			return a;
		}
	}
	return Address();
}

void Block_manager_parent::register_erase_outcome(Event const& event, enum status status) {
	IO_has_completed_since_last_shortest_queue_search = true;

	Address a = event.get_address();
	a.valid = PAGE;
	a.page = 0;
	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];

	if (b->get_age() > max_age) {
		max_age = b->get_age();
		//StateVisualiser::print_block_ages();
	}
	Wear_Level(event);

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
			IOScheduler::instance()->schedule_event(new_erase);
		}
	}

	uint age_class = sort_into_age_class(a);
	free_blocks[a.package][a.die][age_class].push_back(a);


	blocks_being_garbage_collected.erase(b->get_physical_address());
	num_blocks_being_garbaged_collected_per_LUN[a.package][a.die]--;

	if (PRINT_LEVEL > 1) {
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	num_free_pages += BLOCK_SIZE;
	num_available_pages_for_new_writes += BLOCK_SIZE;

	//printf("");
	if (blocks_being_garbage_collected.size() == 0) {
		assert(num_free_pages == num_available_pages_for_new_writes);
	}

	Block_manager_parent::check_if_should_trigger_more_GC(event.get_current_time());
}

void Block_manager_parent::register_write_arrival(Event const& write) {

}

double Block_manager_parent::get_normalised_age(uint age) const {
	double min_age = get_min_age();
	double normalized_age = (age - min_age) / (max_age - min_age);
	assert(normalized_age >= 0 && normalized_age <= 1);
	return normalized_age;
}

void Block_manager_parent::register_register_cleared() {
	IO_has_completed_since_last_shortest_queue_search = true;
}

uint Block_manager_parent::sort_into_age_class(Address const& a) const {
	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	uint age = b->get_age();
	double normalized_age = get_normalised_age(age);
	int klass = floor(normalized_age * num_age_classes * 0.99999);
	return klass;
}

/*bool Block_manager_parent::increment_block_pointer(Address& pointer, Address& block_written_on) {
	if (pointer.compare(pointer) == BLOCK) {
		Address temp = free_block_pointers[pointer.package][pointer.die];
		temp.page = temp.page + 1;
		pointer = temp;
	}
	return !has_free_pages(pointer);
}*/

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
	}
	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE && how_many_gc_operations_are_scheduled() == 0) {
		schedule_gc(event.get_current_time(), -1, -1, -1, -1);
	}
	trim(event);

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
			/*else if (erase_queue[ba.package].size() > 0) {
				Event* erase = erase_queue[ba.package].front();
				erase_queue[ba.package].pop();
				num_erases_scheduled_per_package[ba.package]++;
				IOScheduler::instance()->schedule_event(erase);
			}*/
			else { // If no free pointer could be found, schedule GC
				//schedule_gc(event.get_current_time(), ba.package, ba.die);

			}
			if (PRINT_LEVEL > 1) {
				if (free_pointer.valid == NONE) printf(", and a new unused block could not be found.\n");
				else printf(".\n");
			}
		}
	}
}

void Block_manager_parent::trim(Event const& event) {
	IO_has_completed_since_last_shortest_queue_search = true;

	Address ra = event.get_replace_address();

	if (ra.valid == NONE) {
		return;
	}

	Block& block = ssd.getPackages()[ra.package].getDies()[ra.die].getPlanes()[ra.plane].getBlocks()[ra.block];
	Page const& page = block.getPages()[ra.page];
	uint age_class = sort_into_age_class(ra);
	long const phys_addr = block.get_physical_address();

	assert(block.get_state() == ACTIVE || block.get_state() == PARTIALLY_FREE);

	assert(page.get_state() == VALID);
	if (page.get_state() == VALID) {   // TODO: determine if I should remove the if statement
		block.invalidate_page(ra.page);
	}

	ra.valid = BLOCK;
	ra.page = 0;

	if (blocks_being_garbage_collected.count(block.get_physical_address()) == 1) {
		assert(blocks_being_garbage_collected[block.get_physical_address()] > 0);
		blocks_being_garbage_collected[block.get_physical_address()]--;
	}

	// TODO: fix thresholds for inserting blocks into GC lists. ALSO Revise the condition.
	if (blocks_being_garbage_collected.count(block.get_physical_address()) == 0 &&
			(block.get_state() == ACTIVE || block.get_state() == PARTIALLY_FREE) && block.get_pages_valid() < BLOCK_SIZE) {
		remove_as_gc_candidate(ra);
		if (PRINT_LEVEL > 1) {
			printf("Inserting as GC candidate: %ld ", phys_addr); ra.print(); printf(" with age_class %d and valid blocks: %d\n", age_class, block.get_pages_valid());
		}
		gc_candidates[ra.package][ra.die][age_class].insert(phys_addr);
		if (gc_candidates[ra.package][ra.die][age_class].size() == 1) {
			check_if_should_trigger_more_GC(event.get_current_time());
		}
	}

	if (blocks_being_garbage_collected.count(phys_addr) == 0 && block.get_state() == INACTIVE) {
		remove_as_gc_candidate(ra);
		gc_candidates[ra.package][ra.die][age_class].erase(phys_addr);
		blocks_being_garbage_collected[phys_addr] = 0;
		num_blocks_being_garbaged_collected_per_LUN[ra.package][ra.die]++;
		issue_erase(ra, event.get_current_time());

	}
	else if (blocks_being_garbage_collected.count(phys_addr) == 1 && blocks_being_garbage_collected[phys_addr] == 0) {
		assert(block.get_state() == INACTIVE);
		blocks_being_garbage_collected[phys_addr]--;
		issue_erase(ra, event.get_current_time());
	}
}

void Block_manager_parent::remove_as_gc_candidate(Address const& phys_address) {
	for (int i = 0; i < num_age_classes; i++) {
		gc_candidates[phys_address.package][phys_address.die][i].erase(phys_address.get_linear_address());
	}
}

void Block_manager_parent::issue_erase(Address ra, double time) {
	ra.valid = BLOCK;
	ra.page = 0;

	Event* erase = new Event(ERASE, 0, 1, time);
	erase->set_address(ra);
		erase->set_garbage_collection_op(true);

	if (PRINT_LEVEL > 1) {
		printf("block %lu", ra.get_linear_address()); printf(" is now invalid. An erase is issued: "); erase->print();
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	//int num_free_blocks = get_num_free_blocks(ra.package, ra.die);
	//assert(num_erases_scheduled_per_package[ra.package] <= 1 && num_erases_scheduled_per_package[ra.package] >= 0);
	bool there_is_already_at_least_one_erase_scheduled_on_this_channel = num_erases_scheduled_per_package[ra.package] > 0;

	if (USE_ERASE_QUEUE && there_is_already_at_least_one_erase_scheduled_on_this_channel /* && num_free_blocks > 0 &&  has_free_pages(free_block_pointers[ra.package][ra.die]) */) {
		erase_queue[ra.package].push(erase);
	}
	else {
		num_erases_scheduled_per_package[ra.package]++;
		IOScheduler::instance()->schedule_event(erase);
	}
}

int Block_manager_parent::get_num_free_blocks(int package, int die) {
	int num_free_blocks = 0;
	for (uint i = 0; i < num_age_classes; i++) {
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
	register_ECC_check_on(event.get_logical_address()); // An ECC check happens in a normal read-write GC operation
	assert(event.get_event_type() == READ_TRANSFER);
}

bool Block_manager_parent::can_write(Event const& write) const {
	return num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();
}

void Block_manager_parent::check_if_should_trigger_more_GC(double start_time) {
	if (num_free_pages <= BLOCK_SIZE) {
		schedule_gc(start_time, -1, -1, -1, -1);
	}

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(free_block_pointers[i][j]) || get_num_free_blocks(i, j) < 1) {
				schedule_gc(start_time, i, j, -1, -1);
			}
		}
	}
}

double Block_manager_parent::get_min_age() const {
	assert(blocks_with_min_age.size() > 0);
	return BLOCK_ERASES - (*blocks_with_min_age.begin())->get_erases_remaining();
}

// TODO, at erase registration, there should be a check for WL queue. If not empty, see if can issue a WL operation. If cannot, issue an emergency GC.
// if the queue is empty, check if should trigger GC.
void Block_manager_parent::Wear_Level(Event const& event) {
	assert(blocks_with_min_age.size() > 0);
	num_erases_up_to_date++;
	Address pba = event.get_address();
	Block* b = &ssd.getPackages()[pba.package].getDies()[pba.die].getPlanes()[pba.plane].getBlocks()[pba.block];

	double time_since_last_erase = event.get_current_time() - b->get_second_last_erase_time();
	average_erase_cycle_time = average_erase_cycle_time * 0.8 + 0.2 * time_since_last_erase;

	if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() > 1) {
		blocks_with_min_age.erase(b);
	}
	else if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() == 1) {
		int min_age = b->get_age() - 1;
		blocks_with_min_age.erase(b);
		update_blocks_with_min_age(min_age + 1);
	}

	if (blocks_being_wl.count(b) > 0) {
		blocks_being_wl.erase(b);
		StatisticsGatherer::get_global_instance()->print();
	}

	// looking for gc candidates every single time to update the list. This is CPU expensive, so we want to make it more seldom in the future.
	if (ENABLE_WEAR_LEVELING /* blocks_to_wl.size() == 0 */ && blocks_being_wl.size() < MAX_ONGOING_WL_OPS && max_age > get_min_age() + WEAR_LEVEL_THRESHOLD) {
		blocks_to_wl.clear();
		find_wl_candidates(event.get_current_time());
		//StateVisualiser::print_block_ages();
		//StatisticsGatherer::get_instance()->print();
	}

	if (blocks_to_wl.size() > 0 && blocks_being_wl.size() < MAX_ONGOING_WL_OPS) {
		int random_index = random_number_generator() % blocks_to_wl.size();
		set<Block*>::iterator i = blocks_to_wl.begin();
		advance(i, random_index);
		Block* target = *i;
		Address addr = Address(target->get_physical_address(), BLOCK);
		if (PRINT_LEVEL > 1) {
			printf("Scheduling WL in "); addr.print(); printf("\n");
		}
		schedule_gc(event.get_current_time(), addr.package, addr.die, addr.block, -1);
	}
}

void Block_manager_parent::update_blocks_with_min_age(uint min_age) {
	for (uint i = 0; i < all_blocks.size(); i++) {
		uint age_ith_block = BLOCK_ERASES - all_blocks[i]->get_erases_remaining();
		if (age_ith_block == min_age) {
			blocks_with_min_age.insert(all_blocks[i]);
		}
	}
}

void Block_manager_parent::find_wl_candidates(double current_time) {
	for (uint i = 0; i < all_blocks.size(); i++) {
		Block* b = all_blocks[i];
		int age = b->get_age();
		double normalised_age = get_normalised_age(age);
		double time_since_last_erase = current_time - b->get_last_erase_time();
		if (b->get_state() == ACTIVE && normalised_age < 0.1 && time_since_last_erase  > average_erase_cycle_time * 10) {
			blocks_to_wl.insert(b);
		}
	}
	if (PRINT_LEVEL > 1) {
		printf("%d elements inserted into WL queue\n", blocks_to_wl.size());
	}
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
			bool die_register_is_busy = ssd.getPackages()[channel_id].getDies()[die_id].register_is_busy();
			if (die_has_free_pages && !die_register_is_busy) {
				can_write = true;
				double channel_finish_time = ssd.bus.get_channel(channel_id).get_currently_executing_operation_finish_time();
				double die_finish_time = ssd.getPackages()[channel_id].getDies()[die_id].get_currently_executing_io_finish_time();
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

uint Block_manager_parent::how_many_gc_operations_are_scheduled() const {
	return blocks_being_garbage_collected.size();
}

bool Block_manager_parent::Copy_backs_in_progress(Address const& addr) {
	/*if (addr.page > 0) {
		Block& block = ssd.getPackages()[addr.package].getDies()[addr.die].getPlanes()[addr.plane].getBlocks()[addr.block];
		Page const& page_before = block.getPages()[addr.page - 1];
		return page_before.get_state() == EMPTY;
	}*/
	return false;
}

// gives time until both the channel and die are clear
double Block_manager_parent::in_how_long_can_this_event_be_scheduled(Address const& address, double event_time) const {
	if (address.valid == NONE) {
		return 10;
	}

	uint package_id = address.package;
	uint die_id = address.die;
	double channel_finish_time = ssd.bus.get_channel(package_id).get_currently_executing_operation_finish_time();
	double die_finish_time = ssd.getPackages()[package_id].getDies()[die_id].get_currently_executing_io_finish_time();
	double max = std::max(channel_finish_time, die_finish_time);

	double time = max - event_time;
	return time < 0 ? 0 : time;
}

bool Block_manager_parent::can_schedule_on_die(Event const* event) const {
	uint package_id = event->get_address().package;
	uint die_id = event->get_address().die;
	bool busy = ssd.getPackages()[package_id].getDies()[die_id].register_is_busy();
	if (!busy) {
		return true;
	}
	uint application_io = ssd.getPackages()[package_id].getDies()[die_id].get_last_read_application_io();
	event_type type = event->get_event_type();
	return (type == READ_TRANSFER || type == COPY_BACK ) && application_io == event->get_application_io_id();
}

bool Block_manager_parent::is_die_register_busy(Address const& addr) const {
	uint package_id = addr.package;
	uint die_id = addr.die;
	return ssd.getPackages()[package_id].getDies()[die_id].register_is_busy();
}

bool Block_manager_parent::can_schedule_write_immediately(Address const& prospective_dest, double current_time) {
	bool free_pages = has_free_pages(prospective_dest);
	bool die_not_busy =		!is_die_register_busy(prospective_dest);
	bool no_copy_back =		!Copy_backs_in_progress(prospective_dest);
	bool can_be_scheduled_now =	in_how_long_can_this_event_be_scheduled(prospective_dest, current_time) == 0;
	return free_pages && die_not_busy && no_copy_back && can_be_scheduled_now;
}

// puts free blocks at the very end of the queue
struct block_valid_pages_comparator_wearwolf {
	bool operator () (const Block * i, const Block * j)
	{
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

/*bool Block_manager_parent::schedule_queued_erase(Address location) {
	int package = location.package;
	int die = location.die;
	if (location.valid == DIE && erase_queue[package][die].size() > 0) {
		Event* erase = erase_queue[package][die].front();
		erase_queue[package][die].pop();
		num_erases_scheduled[package][die]++;
		IOScheduler::instance()->schedule_event(erase);
		return true;
	}
	else if (location.valid == PACKAGE) {
		location.valid = DIE;
		for (uint i = 0; i < PACKAGE_SIZE; i++) {
			location.die = i;
			bool scheduled_an_erase = schedule_queued_erase(location);
			if (scheduled_an_erase) {
				return true;
			}
		}
		return false;
	}
	else if (location.valid == NONE) {
		location.valid = PACKAGE;
		for (uint i = 0; i < SSD_SIZE; i++) {
			location.package = i;
			bool scheduled_an_erase = schedule_queued_erase(location);
			if (scheduled_an_erase) {
				return true;
			}
		}
	}
	return false;
}*/

// schedules a garbage collection operation to occur at a given time, and optionally for a given channel, LUN or age class
// the block to be reclaimed is chosen when the gc operation is initialised
void Block_manager_parent::schedule_gc(double time, int package, int die, int block, int klass) {
	Event *gc = new Event(GARBAGE_COLLECTION, 0, BLOCK_SIZE, time);
	Address address;
	address.package = package;
	address.die = die;
	address.block = block;

	if (package == UNDEFINED && die == UNDEFINED && block == UNDEFINED) {
		address.valid = NONE;
	} else if (package >= 0 && die == UNDEFINED && block == UNDEFINED) {
		address.valid = PACKAGE;
	} else if (package >= 0 && die >= 0 && block == UNDEFINED) {
		address.valid = DIE;
	} else if (package >= 0 && die >= 0 && block >= 0) {
		address.valid = BLOCK;
		gc->set_wear_leveling_op(true);
	} else {
		assert(false);
	}

	gc->set_noop(true);
	gc->set_address(address);
	gc->set_age_class(klass);
	gc->set_garbage_collection_op(true);

	if (PRINT_LEVEL > 1) {
		//StateTracer::print();
		printf("scheduling gc in (%d %d %d %d)  -  ", package, die, block, klass); gc->print();
	}
	IOScheduler::instance()->schedule_event(gc);
}

vector<long> Block_manager_parent::get_relevant_gc_candidates(int package_id, int die_id, int klass) const {
	vector<long > candidates;
	int package = package_id == -1 ? 0 : package_id;
	int num_packages = package_id == -1 ? SSD_SIZE : package_id + 1;
	for (; package < num_packages; package++) {
		int die = die_id == -1 ? 0 : die_id;
		int num_dies = die_id == -1 ? PACKAGE_SIZE : die_id + 1;
		for (; die < num_dies; die++) {
			int age_class = klass == -1 ? 0 : klass;
			int num_classes = klass == -1 ? num_age_classes : klass + 1;
			for (; age_class < num_classes; age_class++) {
				set<long>::iterator iter = gc_candidates[package][die][age_class].begin();
				for (; iter != gc_candidates[package][die][age_class].end(); ++iter) {
					long g = *iter;
					candidates.push_back(*iter);
				}
			}
		}
	}
	if (candidates.size() == 0 && klass != -1) {
		candidates = get_relevant_gc_candidates(package_id, die_id, -1);
	}
	return candidates;
}

Block* Block_manager_parent::choose_gc_victim(vector<long> candidates) const {
	uint min_valid_pages = BLOCK_SIZE;
	Block* best_block = NULL;

	// Old solution
	for (uint i = 0; i < candidates.size(); i++) {
		long physical_address = candidates[i];
		Address a = Address(physical_address, BLOCK);
		Block* block = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
		if (block->get_pages_valid() < min_valid_pages && block->get_state() == ACTIVE) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}

// New solution introducing random tie-breaking, giving no improvement whatsoever.
/*
    MTRand_int32 random_number_generator(time(NULL));
    while (candidates.size() > 0) {
		int i = random_number_generator() % candidates.size();
		long physical_address = candidates[i];
		candidates.erase(candidates.begin() + i);

		Address a = Address(physical_address, BLOCK);
		Block* block = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
		if (block->get_pages_valid() < min_valid_pages && block->get_state() == ACTIVE) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}
*/
	return best_block;
}

// Returns true if a copy back is allowed on a given logical address
bool Block_manager_parent::copy_back_allowed_on(long logical_address) {
	if (MAX_REPEATED_COPY_BACKS_ALLOWED <= 0 || MAX_ITEMS_IN_COPY_BACK_MAP <= 0) return false;
	//map<long, uint>::iterator copy_back_count = page_copy_back_count.find(logical_address);
	bool address_in_map = page_copy_back_count.count(logical_address) == 1; //(copy_back_count != page_copy_back_count.end());
	// If address is not in map and map is full, or if page has already been copy backed as many times as allowed, copy back is not allowed
	if ((!address_in_map && page_copy_back_count.size() >= MAX_ITEMS_IN_COPY_BACK_MAP) ||
		( address_in_map && page_copy_back_count[logical_address] >= MAX_REPEATED_COPY_BACKS_ALLOWED)) return false;
	else return true;
}

// Returns a reserved address from a free block on a chosen package/die
Address Block_manager_parent::reserve_page_on(uint package, uint die, double time) {
	// Try to find a free page on the same die (should actually be plane!), so that a fast copy back can be done
	Address free_block = free_block_pointers[package][die];
	if (!has_free_pages(free_block)) { // If there is no free pages left, try to find another block
		free_block = find_free_unused_block(package, die, time);
		if (free_block.valid == NONE) { // Another free block could not be found, schedule GC and return invalid address
			//schedule_gc(time, package, die);
			return Address(0, NONE);
		}
		assert(free_block.package == package);
		assert(free_block.die == die);
		free_block_pointers[package][die] = free_block;
	}
	increment_pointer(free_block_pointers[package][die]); // Increment pointer so the returned page is not used again in the future (it is reserved)
	assert(has_free_pages(free_block));
	return free_block;
}

// Updates map keeping track of performed copy backs for each logical address
void Block_manager_parent::register_copy_back_operation_on(uint logical_address) {
	page_copy_back_count[logical_address]++; // Increment copy back counter for target page (if address is not yet in map, it will be inserted and count will become 1)
}

// Signals than an ECC check has been performed on a page, meaning that it can be copy backed again in the future
void Block_manager_parent::register_ECC_check_on(uint logical_address) {
	page_copy_back_count.erase(logical_address);
}

void Block_manager_parent::register_trim_making_gc_redundant() {
	if (PRINT_LEVEL > 1) {
		printf("a trim made a gc event redundant\n");
	}
	num_available_pages_for_new_writes++;
}

// Reads and rewrites all valid pages of a block somewhere else
// An erase is issued in register_write_completion after the last
// page from this block has been migrated
vector<deque<Event*> > Block_manager_parent::migrate(Event* gc_event) {
	Address a = gc_event->get_address();
	vector<deque<Event*> > migrations;

	if (how_many_gc_operations_are_scheduled() >= SSD_SIZE * PACKAGE_SIZE * 0.75) {
		return migrations;
	}

	/*bool scheduled_erase_successfully = schedule_queued_erase(a);
	if (scheduled_erase_successfully) {
		return migrations;
	}*/

	int block_id = a.valid >= BLOCK ? a.block : UNDEFINED;
	int die_id = a.valid >= DIE ? a.die : UNDEFINED;
	int package_id = a.valid >= PACKAGE ? a.package : UNDEFINED;


	bool is_wear_leveling_op = gc_event->is_wear_leveling_op();

	Block * victim;
	if (is_wear_leveling_op) {
		victim = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	}
	else {
		vector<long> candidates = get_relevant_gc_candidates(package_id, die_id, gc_event->get_age_class());
		victim = choose_gc_victim(candidates);
	}

	StatisticsGatherer::get_global_instance()->register_scheduled_gc(*gc_event);

	if (victim == NULL) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_no_candidate++;
		return migrations;
	}

	if (num_available_pages_for_new_writes < victim->get_pages_valid()) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_not_enough_free_space++;
		return migrations;
	}

	Address addr = Address(victim->get_physical_address(), BLOCK);

	uint max_num_gc_per_LUN = 1; //GREEDY_GC ? 2 : 1;
	if (num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die] >= max_num_gc_per_LUN) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_gc_already_happening++;
		return migrations;
	}

	/*if (blocks_being_wl.count(victim) == 1) {
		return migrations;
	}*/

	if (is_wear_leveling_op) {
		if (blocks_being_wl.size() >= MAX_ONGOING_WL_OPS) {
			return migrations;
		} else {
			blocks_being_wl.insert(victim);
		}
	}

	blocks_to_wl.erase(victim);
	remove_as_gc_candidate(addr);

	blocks_being_garbage_collected[victim->get_physical_address()] = victim->get_pages_valid();
	num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die]++;

	if (PRINT_LEVEL > 1) {
		printf("num gc operations in (%d %d) : %d  ", addr.package, addr.die, num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die]);
		printf("Triggering GC in %ld    time: %f  ", victim->get_physical_address(), gc_event->get_current_time()); addr.print(); printf(". Migrating %d \n", victim->get_pages_valid());
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	assert(victim->get_state() != FREE);
	assert(victim->get_state() != PARTIALLY_FREE);

	num_available_pages_for_new_writes -= victim->get_pages_valid();

	//deque<Event*> cb_migrations; // We put all copy back GC operations on one deque and push it on migrations vector. This makes the CB migrations happen in order as they should.
	StatisticsGatherer::get_global_instance()->register_executed_gc(*gc_event, *victim);

	// TODO: for DFTL, we in fact do not know the LBA when we dispatch the write. We get this from the OOB. Need to fix this.
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		if (victim->getPages()[i].get_state() == VALID) {

			Address addr = Address(victim->physical_address, PAGE);
			addr.page = i;
			long logical_address = ftl.get_logical_address(addr.get_linear_address());

			deque<Event*> migration;

			// If a copy back is allowed, and a target page could be reserved, do it. Otherwise, just do a traditional and more expensive READ - WRITE garbage collection
			if (copy_back_allowed_on(logical_address)) {

				Event* read_command = new Event(READ_COMMAND, logical_address, 1, gc_event->get_start_time());
				read_command->set_address(addr);
				read_command->set_garbage_collection_op(true);

				Event* copy_back = new Event(COPY_BACK, logical_address, 1, gc_event->get_start_time());
				copy_back->set_replace_address(addr);
				copy_back->set_garbage_collection_op(true);

				migration.push_back(read_command);
				migration.push_back(copy_back);
				register_copy_back_operation_on(logical_address);
				//printf("COPY_BACK MAP (Size: %d):\n", page_copy_back_count.size()); for (map<long, uint>::iterator it = page_copy_back_count.begin(); it != page_copy_back_count.end(); it++) printf(" lba %d\t: %d\n", it->first, it->second);
			} else {
				Event* read = new Event(READ, logical_address, 1, gc_event->get_start_time());
				read->set_address(addr);
				read->set_garbage_collection_op(true);

				Event* write = new Event(WRITE, logical_address, 1, gc_event->get_start_time());
				write->set_garbage_collection_op(true);
				write->set_replace_address(addr);

				if (is_wear_leveling_op) {
					read->set_wear_leveling_op(true);
					write->set_wear_leveling_op(true);
				}

				migration.push_back(read);
				migration.push_back(write);

				//register_ECC_check_on(logical_address); // An ECC check happens in a normal read-write GC operation
			}
			migrations.push_back(migration);
		}
	}
	//if (cb_migrations.size() > 0) migrations.push_back(cb_migrations);

	return migrations;
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parent::find_free_unused_block(double time) {
	vector<int> order = order_randomiser.get_iterator(SSD_SIZE);
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
	vector<int> order = order_randomiser.get_iterator(PACKAGE_SIZE);
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
	vector<int> order = order_randomiser.get_iterator(num_age_classes);
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
	if (num_free_blocks_left < GREED_SCALE) {
		schedule_gc(time, package_id, die_id, -1, -1);
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
	vector<int> order1 = order_randomiser.get_iterator(SSD_SIZE);
	for (uint i = 0; i < SSD_SIZE; i++) {
		int package = order1[i];
		vector<int> order2 = order_randomiser.get_iterator(PACKAGE_SIZE);
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

void Block_manager_parent::return_unfilled_block(Address pba) {
	if (has_free_pages(pba)) {
		int age_class = sort_into_age_class(pba);
		if (has_free_pages(free_block_pointers[pba.package][pba.die])) {
			free_blocks[pba.package][pba.die][age_class].push_back(pba);
		} else {
			free_block_pointers[pba.package][pba.die] = pba;
		}

	}
}

