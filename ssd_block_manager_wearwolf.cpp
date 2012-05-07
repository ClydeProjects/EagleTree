
#include "ssd.h"

using namespace ssd;

Block_manager_parallel_wearwolf::Block_manager_parallel_wearwolf(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel(ssd, ftl),
	  page_hotness_measurer()
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

	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;
	Address block_address = Address(event.get_address().get_linear_address(), BLOCK);
	int num_pages_written = -1;
	if (block_address.compare(free_block_pointers[package_id][die_id])) {
		Address pointer = free_block_pointers[package_id][die_id];
		pointer.page = num_pages_written = pointer.page + 1;
		free_block_pointers[package_id][die_id] = pointer;
	}
	else if (block_address.compare(wcrh_pointer)) {
		wcrh_pointer.page = num_pages_written = wcrh_pointer.page + 1;
	}
	else if (block_address.compare(wcrc_pointer)) {
		wcrc_pointer.page = num_pages_written = wcrc_pointer.page + 1;
	}

	num_free_pages--;
	num_available_pages_for_new_writes--;

	// if there are very few pages left, need to trigger emergency GC
	if (num_free_pages <= BLOCK_SIZE) {
		Block_manager_parallel::Garbage_Collect(event.get_start_time() + event.get_time_taken());
		return;
	}

	//there is still more room in this pointer, so no need to trigger GC
	if (num_pages_written < BLOCK_SIZE) {
		return;
	}

	if (block_address.compare(free_block_pointers[package_id][die_id])) {
		Address free_block = find_free_unused_block(package_id, die_id);
		if (free_block.valid != NONE) {
			free_block_pointers[package_id][die_id] = free_block;
		} else {
			Garbage_Collect(package_id, die_id, event.get_start_time() + event.get_time_taken());
		}
	} else if (block_address.compare(wcrh_pointer)) {
		Address addr = page_hotness_measurer.get_die_with_least_wcrh();
		Address free_block = find_free_unused_block(addr.package, addr.die);
		if (free_block.valid != NONE) {
			wcrh_pointer = free_block;
		} else {
			Garbage_Collect(addr.package, addr.die, event.get_start_time() + event.get_time_taken());
		}
	} else if (block_address.compare(wcrc_pointer)) {
		Address addr = page_hotness_measurer.get_die_with_least_wcrc();
		Address free_block = find_free_unused_block(addr.package, addr.die);
		if (free_block.valid != NONE) {
			wcrc_pointer = free_block;
		} else {
			Garbage_Collect(addr.package, addr.die, event.get_start_time() + event.get_time_taken());
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

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (free_block_pointers[package_id][die_id].page >= BLOCK_SIZE) {
		free_block_pointers[package_id][die_id] = event.get_address();
	}
	else if (wcrh_pointer.page >= BLOCK_SIZE) {
		wcrh_pointer = event.get_address();
	} else if (wcrc_pointer.page >= BLOCK_SIZE) {
		wcrc_pointer = event.get_address();
	}

	// update num_free_pages
	num_free_pages += BLOCK_SIZE;

	// check if there are any dies on which there are no free pointers. Trigger GC on them.

	num_available_pages_for_new_writes += BLOCK_SIZE;

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
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
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_address().get_linear_address());
	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(write.get_address().get_linear_address());

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
	Block_manager_parallel::check_if_should_trigger_more_GC(start_time);
	if (wcrh_pointer.page >= BLOCK_SIZE) {
		Address addr = page_hotness_measurer.get_die_with_least_wcrh();
		Address free_block = find_free_unused_block(addr.package, addr.die);
		if (free_block.valid != NONE) {
			wcrh_pointer = free_block;
		} else {
			Garbage_Collect(addr.package, addr.die, start_time);
		}
	}
	if (wcrc_pointer.page >= BLOCK_SIZE) {
		Address addr = page_hotness_measurer.get_die_with_least_wcrc();
		Address free_block = find_free_unused_block(addr.package, addr.die);
		if (free_block.valid != NONE) {
			wcrc_pointer = free_block;
		} else {
			Garbage_Collect(addr.package, addr.die, start_time);
		}
	}
}

Address Block_manager_parallel_wearwolf::choose_write_location(Event const& event) const {
	// if GC, try writing in appropriate pointer. If that doesn't work, write anywhere free.
	// if not
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(event.get_address().get_linear_address());
	bool wh_available = at_least_one_available_write_hot_pointer();

	if (wh_available && w_hotness == WRITE_HOT) {
		return Block_manager_parallel::choose_write_location(event);
	}

	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(event.get_address().get_linear_address());
	bool wcrc_available = pointer_can_be_written_to(wcrc_pointer);

	if (wcrc_available && w_hotness == WRITE_COLD && r_hotness == READ_COLD ) {
		return wcrc_pointer;
	}

	bool wcrh_available = pointer_can_be_written_to(wcrh_pointer);

	if (wcrh_available && w_hotness == WRITE_COLD && r_hotness == READ_HOT ) {
		return wcrh_pointer;
	}

	// if we are here, we must make a mistake. Simply choose some free pointer.

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
struct block_valid_pages_comparator {
	bool operator () (const Block * i, const Block * j)
	{
		if (i->get_state() == FREE){
			return false;
		}
		if (i->get_state() == PARTIALLY_FREE && j->get_state() == FREE) {
			return true;
		}
		return i->get_pages_invalid() > j->get_pages_invalid();
	}
};

void Block_manager_parallel_wearwolf::Garbage_Collect(uint package_id, uint die_id, double start_time) {
	std::sort(blocks[package_id][die_id].begin(), blocks[package_id][die_id].end(), block_valid_pages_comparator());

	Block *target = blocks[package_id][die_id][0];
	assert(target->get_state() != FREE && target->get_state() != PARTIALLY_FREE);

	if (num_available_pages_for_new_writes < target->get_pages_valid()) {
		printf("tried to GC from die (%d %d), but not enough free pages to migrate all valid pages\n", package_id, die_id);
		return;
	}
	printf("triggering GC in die (%d %d)\n", package_id, die_id);
	num_available_pages_for_new_writes -= target->get_pages_valid();

	migrate(target, start_time);

}

Address Block_manager_parallel_wearwolf::find_free_unused_block(uint package_id, uint die_id) {
	std::sort(blocks[package_id][die_id].begin(), blocks[package_id][die_id].end(), block_valid_pages_comparator());
	int i = blocks[package_id][die_id].size() - 1;
	Block *target = blocks[package_id][die_id][i];
	while (target->get_state() == FREE) {
		Address ba = new Address(target->get_physical_address(), BLOCK);
		if (!ba.compare(free_block_pointers[package_id][die_id]) &&
			!ba.compare(wcrh_pointer) &&
			!ba.compare(wcrc_pointer))
		{
			return ba;
		}
		i--;
	}
	return Address(0, NONE);
}
