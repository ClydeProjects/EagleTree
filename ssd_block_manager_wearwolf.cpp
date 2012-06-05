
#include "ssd.h"

using namespace ssd;

Block_manager_parallel_wearwolf::Block_manager_parallel_wearwolf(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel(ssd, ftl),
	  page_hotness_measurer(),
	  blocks_currently_undergoing_gc(),
	  enable_cold_data_balancing(true)
{
	if (SSD_SIZE > 1) {
		wcrh_pointer = Address(0,0,0,1,0, PAGE);
		wcrc_pointer = Address(1,0,0,1,0, PAGE);
	} else if (PACKAGE_SIZE > 1) {
		wcrh_pointer = Address(0,0,0,1,0, PAGE);
		wcrc_pointer = Address(0,1,0,1,0, PAGE);
	} else {
		wcrh_pointer = Address(0,0,0,1,0, PAGE);
		wcrc_pointer = Address(0,0,0,2,0, PAGE);
	}
	wcrh_pointer.print();
	printf("\n");
	wcrc_pointer.print();
	printf("\n");
}

Block_manager_parallel_wearwolf::~Block_manager_parallel_wearwolf(void) {}

void Block_manager_parallel_wearwolf::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}

	if (!event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}

	// Increment block pointer
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	Address block_address = Address(event.get_address().get_linear_address(), BLOCK);
	uint num_pages_written = -1;
	if (block_address.compare(free_block_pointers[package_id][die_id]) == BLOCK) {
		Address pointer = free_block_pointers[package_id][die_id];
		pointer.page = num_pages_written = pointer.page + 1;
		free_block_pointers[package_id][die_id] = pointer;
	}
	else if (block_address.compare(wcrh_pointer) == BLOCK) {
		wcrh_pointer.page = num_pages_written = wcrh_pointer.page + 1;
	}
	else if (block_address.compare(wcrc_pointer) == BLOCK) {
		wcrc_pointer.page = num_pages_written = wcrc_pointer.page + 1;
	}

	// Update stats about free pages
	assert(num_free_pages > 0);
	num_free_pages--;
	if (!event.is_garbage_collection_op()) {
		assert(num_available_pages_for_new_writes > 0);
		num_available_pages_for_new_writes--;
	}

	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE) {
		Garbage_Collect(event.get_start_time() + event.get_time_taken());
		return;
	}

	// there is still more room in this pointer, so no need to trigger GC
	if (num_pages_written < BLOCK_SIZE) {
		return;
	}

	// check if the pointer if full. If it is, find a free block for a new pointer, or trigger GC if there are no free blocks
	if (block_address.compare(free_block_pointers[package_id][die_id]) == BLOCK) {
		printf("hot pointer ");
		free_block_pointers[package_id][die_id].print();
		printf(" is out of space\n");
		Address free_block = find_free_unused_block(package_id, die_id);
		if (free_block.valid != NONE) {
			free_block_pointers[package_id][die_id] = free_block;
		} else {
			Garbage_Collect(package_id, die_id, event.get_start_time() + event.get_time_taken());
		}
	} else if (block_address.compare(wcrh_pointer) == BLOCK) {
		handle_cold_pointer_out_of_space(READ_HOT, event.get_start_time() + event.get_time_taken());
	} else if (block_address.compare(wcrc_pointer) == BLOCK) {
		handle_cold_pointer_out_of_space(READ_COLD, event.get_start_time() + event.get_time_taken());
	}
}

void Block_manager_parallel_wearwolf::handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time) {
	Address addr = page_hotness_measurer.get_die_with_least_WC(rh);
	Address& pointer = rh == READ_COLD ? wcrc_pointer : wcrh_pointer;
	if (enable_cold_data_balancing) {
		Address free_block = find_free_unused_block();
		if (free_block.valid != NONE) {
			pointer = free_block;
		} else {
			Garbage_Collect(start_time);
		}
	}
	else {
		Address free_block = find_free_unused_block(addr.package, addr.die);
		if (free_block.valid != NONE) {
			pointer = free_block;
		} else {
			Garbage_Collect(addr.package, addr.die, start_time);
		}
	}
}

void Block_manager_parallel_wearwolf::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	Address addr = event.get_address();
	addr.valid = PAGE;
	addr.page = 0;

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (free_block_pointers[package_id][die_id].page >= BLOCK_SIZE) {
		free_block_pointers[package_id][die_id] = addr;
	}
	else if (wcrh_pointer.page >= BLOCK_SIZE) {
		wcrh_pointer = addr;
	} else if (wcrc_pointer.page >= BLOCK_SIZE) {
		wcrc_pointer = addr;
	}

	// update num_free_pages
	num_free_pages += BLOCK_SIZE;
	num_available_pages_for_new_writes += BLOCK_SIZE;

	long phys_addr = event.get_address().get_linear_address();
	assert(blocks_currently_undergoing_gc.count(phys_addr) == 1);
	blocks_currently_undergoing_gc.erase(phys_addr);
	assert(blocks_currently_undergoing_gc.count(phys_addr) == 0);

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
	Wear_Level(event);
}

// ensures the pointer has at least 1 free page, and that the die is not busy (waiting for a read)
bool Block_manager_parallel_wearwolf::pointer_can_be_written_to(Address pointer) const {
	bool has_space = pointer.page < BLOCK_SIZE;
	bool non_busy = !ssd.getPackages()[pointer.package].getDies()[pointer.die].register_is_busy();
	return has_space && non_busy;
}


bool Block_manager_parallel_wearwolf::at_least_one_available_write_hot_pointer() const  {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (pointer_can_be_written_to(free_block_pointers[i][j])) {
				return true;
			}
		}
	}
	return false;
}


/*
 * makes sure that there is at least 1 non-busy die with free space
 * and that the die is not waiting for an impending read transfer
 */
bool Block_manager_parallel_wearwolf::can_write(Event const& write) const {
	if (num_available_pages_for_new_writes == 0 && !write.is_garbage_collection_op()) {
		return false;
	}

	bool wh_available = at_least_one_available_write_hot_pointer();
	bool wcrc_available = pointer_can_be_written_to(wcrc_pointer);
	bool wcrh_available = pointer_can_be_written_to(wcrh_pointer);

	if (write.is_garbage_collection_op()) {
		return wh_available || wcrc_available || wcrh_available;
	}

	// left with norm
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(write.get_logical_address());

	if (w_hotness == WRITE_HOT) {
		return wh_available;
	} else if (r_hotness == READ_HOT) {
		return wcrh_available;
	} else {
		return wcrc_available;
	}
	assert(false);
	return false;
}

void Block_manager_parallel_wearwolf::register_read_outcome(Event const& event, enum status status){
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Block_manager_parallel_wearwolf::check_if_should_trigger_more_GC(double start_time) {
	if (num_free_pages <= BLOCK_SIZE) {
		Garbage_Collect(start_time);
		return;
	}
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (free_block_pointers[i][j].page >= BLOCK_SIZE) {
				Garbage_Collect(i, j, start_time);
			}
		}
	}
	if (wcrh_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_HOT, start_time);
	}
	if (wcrc_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_COLD, start_time);
	}
}

Address Block_manager_parallel_wearwolf::choose_write_location(Event const& event) const {
	// if GC, try writing in appropriate pointer. If that doesn't work, write anywhere free.
	// if not
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(event.get_logical_address());
	bool wh_available = at_least_one_available_write_hot_pointer();

	// TODO: if write-hot, need to assign READ_HOT to non-busy planes and READ_COLD to busy planes. Do this while still trying to write to a die with a short queue
	if (wh_available && w_hotness == WRITE_HOT) {
		//printf("WRITE_HOT\n");
		return Block_manager_parallel::choose_write_location(event);
	}

	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(event.get_logical_address());
	bool wcrc_available = pointer_can_be_written_to(wcrc_pointer);

	if (wcrc_available && w_hotness == WRITE_COLD && r_hotness == READ_COLD ) {
		printf("WRITE_COLD READ_COLD\n");
		return wcrc_pointer;
	}

	bool wcrh_available = pointer_can_be_written_to(wcrh_pointer);

	if (wcrh_available && w_hotness == WRITE_COLD && r_hotness == READ_HOT ) {
		printf("WRITE_COLD READ_HOT\n");
		return wcrh_pointer;
	}
	printf("MISTAKE\n");
	// if we are here, we must make a mistake. Simply choose some free pointer.
	// can only get here if can_write returned true. It only allows mistakes for GC
	assert(event.is_garbage_collection_op());

	if (wh_available) {
		return Block_manager_parallel::choose_write_location(event);
	}

	if (wcrc_available) {
		return wcrc_pointer;
	}

	if (wcrh_available) {
		return wcrh_pointer;
	}
	assert(false);
	return NULL;
}

// puts free blocks at the very end of the queue
struct block_valid_pages_comparator_wearwolf {
	bool operator () (const Block * i, const Block * j)
	{
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

// GC from the cheapest block in the device.
void Block_manager_parallel_wearwolf::Garbage_Collect(double start_time) {
	// first, find the cheapest block
	std::sort(all_blocks.begin(), all_blocks.end(), block_valid_pages_comparator_wearwolf());

	assert(num_free_pages <= BLOCK_SIZE);

	if (num_available_pages_for_new_writes < BLOCK_SIZE) {
		assert(blocks_currently_undergoing_gc.size() > 0);
		return;
	}
	//assert(num_available_pages_for_new_writes == BLOCK_SIZE);

	Block *target;
	bool found_suitable_block = false;
	for (uint i = 0; i < all_blocks.size(); i++) {
		target = all_blocks[i];
		if (blocks_currently_undergoing_gc.count(target->get_physical_address()) == 0 &&
				target->get_state() != FREE &&
				target->get_state() != PARTIALLY_FREE) {
			found_suitable_block = true;
			break;
		}
	}
	assert(num_available_pages_for_new_writes >= target->get_pages_valid());
	assert(found_suitable_block);

	num_available_pages_for_new_writes -= target->get_pages_valid();
	blocks_currently_undergoing_gc.insert(target->get_physical_address());

	printf("Triggering emergency GC. Only %d free pages left.  ", num_free_pages);
	Address a = Address(target->get_physical_address(), BLOCK);
	a.print();
	printf("\n");

	migrate(target, start_time);
}

void Block_manager_parallel_wearwolf::Garbage_Collect(uint package_id, uint die_id, double start_time) {
	std::sort(blocks[package_id][die_id].begin(), blocks[package_id][die_id].end(), block_valid_pages_comparator_wearwolf());

	Block *target;
	bool found_suitable_block = false;
	for (uint i = 0; i < blocks[package_id][die_id].size(); i++) {
		target = blocks[package_id][die_id][i];
		if (blocks_currently_undergoing_gc.count(target->get_physical_address()) == 0 &&
				target->get_state() != FREE &&
				target->get_state() != PARTIALLY_FREE &&
				target->get_pages_valid() < BLOCK_SIZE &&
				num_available_pages_for_new_writes >= target->get_pages_valid()) {
			found_suitable_block = true;
			break;
		}
	}
	//if (!found_suitable_block && blocks_currently_undergoing_gc.size() == 0) {
	//	assert(false);
	//}
	if (!found_suitable_block) {
		return;
	}

	assert(target->get_state() != FREE && target->get_state() != PARTIALLY_FREE && target->get_pages_valid() <= num_available_pages_for_new_writes);

	blocks_currently_undergoing_gc.insert(target->get_physical_address());
	num_available_pages_for_new_writes -= target->get_pages_valid();

	printf("triggering GC in ");
	Address a = Address(target->get_physical_address(), BLOCK);
	a.print();
	printf("  %d invalid,  %d valid", target->get_pages_invalid(), target->get_pages_valid());
	printf("\n");

	migrate(target, start_time);
}

// finds and returns a free block from anywhere in the SSD. Returns Address(0, NONE) is there is no such block
Address Block_manager_parallel_wearwolf::find_free_unused_block() {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			Address address = find_free_unused_block(i, j);
			if (address.valid != NONE) {
				return address;
			}
		}
	}
	return Address(0, NONE);
}

// finds and returns a free block a spesific die. Returns Address(0, NONE) is there is no such block
Address Block_manager_parallel_wearwolf::find_free_unused_block(uint package_id, uint die_id) {
	Block *target;
	for (uint j = 0; j < blocks[package_id][die_id].size(); j++) {
		target = blocks[package_id][die_id][j];
		Address ba = Address(target->get_physical_address(), BLOCK);
		if (	target->get_state() == FREE &&
				ba.compare(free_block_pointers[package_id][die_id]) != BLOCK &&
				ba.compare(wcrh_pointer) != BLOCK &&
				ba.compare(wcrc_pointer) != BLOCK
			)
		{
			ba.valid = PAGE;
			ba.page = 0;
			return ba;
		}
	}
	return Address(0, NONE);
}
