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
   greedy_gc(GREEDY_GC),
   max_age(1),
   min_age(0),
   num_age_classes(num_age_classes),
   blocks_with_min_age(),
   num_free_pages(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   num_available_pages_for_new_writes(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE),
   blocks_being_garbage_collected(),
   gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(num_age_classes, set<long>()))),
   num_blocks_being_garbaged_collected_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0))
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

Block_manager_parent::~Block_manager_parent(void){}

void Block_manager_parent::register_erase_outcome(Event const& event, enum status status) {
	Address a = event.get_address();
	a.valid = PAGE;
	a.page = 0;

	uint age_class = sort_into_age_class(a);
	free_blocks[a.package][a.die][age_class].push_back(a);

	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
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

	Block_manager_parent::check_if_should_trigger_more_GC(event.get_current_time());
	Wear_Level(event);
}

void Block_manager_parent::register_write_arrival(Event const& write) {

}

uint Block_manager_parent::sort_into_age_class(Address const& a) {
	Block* b = &ssd.getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
	uint age = BLOCK_ERASES - b->get_erases_remaining();
	if (age > max_age) {
		max_age = age;
	}
	double normalized_age = (double)(age - min_age) / (max_age - min_age);
	uint klass = floor(normalized_age * num_age_classes * 0.99999);
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
	assert(num_free_pages > 0);
	num_free_pages--;

	if (!event.is_garbage_collection_op()) {
		assert(num_available_pages_for_new_writes > 0);
		num_available_pages_for_new_writes--;
	}
	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE && how_many_gc_operations_are_scheduled() == 0) {
		schedule_gc(event.get_current_time());
	}
	trim(event);

	Address ba = Address(event.get_address().get_linear_address(), BLOCK);
	if (ba.compare(free_block_pointers[ba.package][ba.die]) == BLOCK && event.get_event_type() == WRITE) {
		increment_pointer(free_block_pointers[ba.package][ba.die]);
		if (!has_free_pages(free_block_pointers[ba.package][ba.die])) {
			printf("hot pointer ");
			free_block_pointers[ba.package][ba.die].print();
			printf(" is out of space");
			Address free_pointer = find_free_unused_block(ba.package, ba.die, event.get_current_time());
			if (has_free_pages(free_pointer)) {
				free_block_pointers[ba.package][ba.die] = free_pointer;
			} else { // If no free pointer could be found, schedule GC
				//schedule_gc(event.get_current_time(), ba.package, ba.die);
			}
			if (free_pointer.valid == NONE) printf(", and a new unused block could not be found.\n");
			else printf(".\n");
		}
	}
}

void Block_manager_parent::trim(Event const& event) {

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
	else if (blocks_being_garbage_collected.count(phys_addr) == 1 && blocks_being_garbage_collected[block.get_physical_address()] == 0) {
		assert(block.get_state() == INACTIVE);
		blocks_being_garbage_collected[phys_addr]--;
		issue_erase(ra, event.get_current_time());
	}
}

void Block_manager_parent::remove_as_gc_candidate(Address const& phys_address) {
	for (uint i = 0; i < num_age_classes; i++) {
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


	IOScheduler::instance()->schedule_event(erase);
}

void Block_manager_parent::register_read_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == READ_COMMAND);
}

bool Block_manager_parent::can_write(Event const& write) const {
	return num_available_pages_for_new_writes > 0 || write.is_garbage_collection_op();
}

void Block_manager_parent::check_if_should_trigger_more_GC(double start_time) {
	if (num_free_pages <= BLOCK_SIZE) {
		schedule_gc(start_time);
	}
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (!has_free_pages(free_block_pointers[i][j])) {
				schedule_gc(start_time, i, j, 0);
			}
		}
	}
}

// TODO, at erase registration, there should be a check for WL queue. If not empty, see if can issue a WL operation. If cannot, issue an emergency GC.
// if the queue is empty, check if should trigger GC.
void Block_manager_parent::Wear_Level(Event const& event) {
	Address pba = event.get_address();
	Block* b = &ssd.getPackages()[pba.package].getDies()[pba.die].getPlanes()[pba.plane].getBlocks()[pba.block];
	uint age = BLOCK_ERASES - b->get_erases_remaining();
	uint min_age = BLOCK_ERASES - (*blocks_with_min_age.begin())->get_erases_remaining();
	std::queue<Block*> blocks_to_wl;
	if (age > max_age) {
		max_age = age;
		uint age_diff = max_age - min_age;
		if (age_diff > 500 && blocks_to_wl.size() == 0) {
			for (std::set<Block*>::const_iterator pos = blocks_with_min_age.begin(); pos != blocks_with_min_age.end(); pos++) {
				blocks_to_wl.push(*pos);
			}
			update_blocks_with_min_age(min_age + 1);
		}
	}
	else if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() > 1) {
		blocks_with_min_age.erase(b);
	}
	else if (blocks_with_min_age.count(b) == 1 && blocks_with_min_age.size() == 1) {
		blocks_with_min_age.erase(b);
		update_blocks_with_min_age(min_age);
		min_age++;
	}

	while (!blocks_to_wl.empty() && num_available_pages_for_new_writes > blocks_to_wl.front()->get_pages_valid()) {
		Block* target = blocks_to_wl.front();
		blocks_to_wl.pop();
		num_available_pages_for_new_writes -= target->get_pages_valid();
		//migrate(target, event.get_start_time() + event.get_time_taken());
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

// This function takes a vector of channels, each of each has a vector of dies
// it finds the die with the shortest queue, and returns its ID
// if all dies are busy, the boolean field is returned as false
pair<bool, pair<uint, uint> > Block_manager_parent::get_free_block_pointer_with_shortest_IO_queue(vector<vector<Address> > const& dies) const {
	uint best_channel_id;
	uint best_die_id;
	bool can_write = false;
	bool at_least_one_address = false;
	double shortest_time = std::numeric_limits<double>::max( );
	for (uint i = 0; i < dies.size(); i++) {
		double earliest_die_finish_time = std::numeric_limits<double>::max();
		uint die_with_earliest_finish_time = 0;
		for (uint j = 0; j < dies[i].size(); j++) {
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
	return pair<bool, pair<uint, uint> >(can_write, pair<uint, uint>(best_channel_id, best_die_id));
}

Address Block_manager_parent::get_free_block_pointer_with_shortest_IO_queue() const {
	pair<bool, pair<uint, uint> > best_die = get_free_block_pointer_with_shortest_IO_queue(free_block_pointers);
	if (!best_die.first) {
		return Address();
	} else {
		return free_block_pointers[best_die.second.first][best_die.second.second];
	}
}

uint Block_manager_parent::how_many_gc_operations_are_scheduled() const {
	return blocks_being_garbage_collected.size();
}

bool Block_manager_parent::has_free_pages(Address const& address) const {
	return address.valid == PAGE && address.page < BLOCK_SIZE;
}

// gives time until both the channel and die are clear
double Block_manager_parent::in_how_long_can_this_event_be_scheduled(Address const& die_address, double time_taken) const {
	if (die_address.valid == NONE) {
		return 1;
	}
	uint package_id = die_address.package;
	uint die_id = die_address.die;
	double channel_finish_time = ssd.bus.get_channel(package_id).get_currently_executing_operation_finish_time();
	double die_finish_time = ssd.getPackages()[package_id].getDies()[die_id].get_currently_executing_io_finish_time();
	double max = std::max(channel_finish_time, die_finish_time);
	double time = max - time_taken;
	return time < 0 ? 0 : time;
}

// puts free blocks at the very end of the queue
struct block_valid_pages_comparator_wearwolf {
	bool operator () (const Block * i, const Block * j)
	{
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

// schedules a garbage collection operation to occur at a given time, and optionally for a given channel, LUN or age class
// the block to be reclaimed is chosen when the gc operation is initialised
void Block_manager_parent::schedule_gc(double time, int package_id, int die_id, int klass) {

	if ( klass >= num_age_classes || package_id >= SSD_SIZE || die_id >= PACKAGE_SIZE) {
		int i = 0;
		i++;
	}
	//assert(package_id < SSD_SIZE && package_id < PACKAGE_SIZE && klass < num_age_classes);

	Address address;
	address.package = package_id;
	address.die = die_id;
	if (package_id == -1 && die_id == -1) {
		address.valid = NONE;
	} else if (package_id >= 0 && die_id == -1) {
		address.valid = PACKAGE;
	} else {
		address.valid = DIE;
	}

	Event *gc = new Event(GARBAGE_COLLECTION, 0, BLOCK_SIZE, time);
	gc->set_noop(true);
	gc->set_address(address);
	gc->set_age_class(klass);
	if (PRINT_LEVEL > 0) {
		//StateTracer::print();
		printf("scheduling gc in (%d %d %d)  -  ", package_id, die_id, klass); gc->print();
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

// Reads and rewrites all valid pages of a block somewhere else
// An erase is issued in register_write_completion after the last
// page from this block has been migrated
vector<deque<Event*> > Block_manager_parent::migrate(Event* gc_event) {

	//StateTracer::print();

	Address a = gc_event->get_address();

	int die_id = a.valid >= DIE ? a.die : -1;
	int package_id = a.valid >= PACKAGE ? a.package : -1;

	vector<long> candidates = get_relevant_gc_candidates(package_id, die_id, gc_event->get_age_class());
	Block * victim = choose_gc_victim(candidates);

	vector<deque<Event*> > migrations;

	if (victim == NULL) {
		return migrations;
	}

	if (num_available_pages_for_new_writes < victim->get_pages_valid()) {
		return migrations;
	}

	Address addr = Address(victim->get_physical_address(), BLOCK);

	uint max_num_gc_per_LUN = GREEDY_GC ? 2 : 1;
	if (num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die] >= max_num_gc_per_LUN) {
		return migrations;
	}

	remove_as_gc_candidate(addr);
	blocks_being_garbage_collected[victim->get_physical_address()] = victim->get_pages_valid();
	num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die]++;
	if (PRINT_LEVEL > 1) {
		printf("num gc operations in (%d %d) : %d", addr.package, addr.die, num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die]);
		printf("Triggering GC in %ld    time: %f  ", victim->get_physical_address(), gc_event->get_current_time()); addr.print(); printf(". Migrating %d \n", victim->get_pages_valid());
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (map<int, int>::iterator iter = blocks_being_garbage_collected.begin(); iter != blocks_being_garbage_collected.end(); iter++) {
			printf("%d  ", (*iter).first);
		}
		printf("\n");
	}

	// if there is not enough free space to migrate the block into, cancel the GC operation

	assert(victim->get_state() != FREE);
	assert(victim->get_state() != PARTIALLY_FREE);

	num_available_pages_for_new_writes -= victim->get_pages_valid();

	// TODO: for DFTL, we in fact do not know the LBA when we dispatch the write. We get this from the OOB. Need to fix this.
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		if (victim->getPages()[i].get_state() == VALID) {

			Address addr = Address(victim->physical_address, PAGE);
			addr.page = i;
			long logical_address = ftl.get_logical_address(addr.get_linear_address());

			deque<Event*> migration;

			// If a copy back is allowed, try to reserve a page on the same die
			Address copy_back_target = copy_back_allowed_on(logical_address) ? reserve_page_on(addr.package, addr.die, gc_event->get_current_time()) : Address(0, NONE);

			// If a copy back is allowed, and a target page could be reserved, do it. Otherwise, just do a traditional and more expensive READ - WRITE garbage collection
			if (copy_back_target.valid == PAGE) {
				Event* copy_back = new Event(COPY_BACK, logical_address, 1, gc_event->get_start_time());
				copy_back->set_address(copy_back_target);
				copy_back->set_replace_address(addr);
				copy_back->set_garbage_collection_op(true);
				printf("Initiating copy-back GC operation ");
				copy_back->print();
				migration.push_back(copy_back);
				register_copy_back_operation_on(logical_address);
				//printf("COPY_BACK MAP (Size: %d):\n", page_copy_back_count.size()); for (map<long, uint>::iterator it = page_copy_back_count.begin(); it != page_copy_back_count.end(); it++) printf(" lba %d\t: %d\n", it->first, it->second);
				//copy_back->print();
			} else {
				Event* read = new Event(READ, logical_address, 1, gc_event->get_start_time());
				read->set_address(addr);
				read->set_garbage_collection_op(true);

				Event* write = new Event(WRITE, logical_address, 1, gc_event->get_start_time());
				write->set_garbage_collection_op(true);
				write->set_replace_address(addr);

				migration.push_back(read);
				migration.push_back(write);

				register_ECC_check_on(logical_address); // An ECC check happens in a normal read-write GC operation
			}
			migrations.push_back(migration);
		}
	}
	return migrations;
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parent::find_free_unused_block(double time) {
	for (uint i = 0; i < SSD_SIZE; i++) {
		Address address = find_free_unused_block(i, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

Address Block_manager_parent::find_free_unused_block(uint package_id, double time) {
	assert(package_id < SSD_SIZE);
	for (uint i = 0; i < PACKAGE_SIZE; i++) {
		Address address = find_free_unused_block(package_id, i, time);
		if (address.valid != NONE) {
			return address;
		}
	}
	return Address(0, NONE);
}

// finds and returns a free block from a particular die in the SSD
Address Block_manager_parent::find_free_unused_block(uint package_id, uint die_id, double time) {
	assert(package_id < SSD_SIZE && die_id < PACKAGE_SIZE);
	for (uint i = 0; i < free_blocks[package_id][die_id].size(); i++) {
		Address address = find_free_unused_block(package_id, die_id, i, time);
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
	uint num_free_blocks_before_gc = GREEDY_GC ? 1 : 0;
	if (num_free_blocks_left <= num_free_blocks_before_gc) {
		schedule_gc(time, package_id, die_id, klass);
	}
	return to_return;
}

Address Block_manager_parent::find_free_unused_block_with_class(uint klass, double time) {
	assert(klass < num_age_classes);
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Address a = free_blocks[i][j][klass].back();
			if (has_free_pages(a)) {
				free_blocks[i][j][klass].pop_back();
				if (greedy_gc && free_blocks[i][j][klass].size() < 2) {
					//perform_gc(i, j, klass, time);
					schedule_gc(time, i, j, klass);
				}
				return a;
			}
		}
	}
	return Address(0, NONE);
}

void Block_manager_parent::return_unfilled_block(Address pba) {
	if (has_free_pages(pba)) {
		int age_class = sort_into_age_class(pba);
		free_blocks[pba.package][pba.die][age_class].push_back(pba);
	}
}

