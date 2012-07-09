
#include "ssd.h"

using namespace ssd;

Block_manager_parallel_wearwolf::Block_manager_parallel_wearwolf(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parent(ssd, ftl, 2),
	  page_hotness_measurer()
{
	wcrh_pointer = find_free_unused_block(0, 0, 0);
	if (SSD_SIZE > 1) {
		wcrc_pointer = find_free_unused_block(1, 0, 0);
	} else if (PACKAGE_SIZE > 1) {
		wcrc_pointer = find_free_unused_block(0, 1, 0);
	} else {
		wcrc_pointer = find_free_unused_block(0, 0, 0);
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
	Block_manager_parent::register_write_outcome(event, status);
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
	} else {
		assert(false);
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
		Address free_block = find_free_unused_block(package_id, die_id, event.get_current_time());
		if (free_block.valid != NONE) {
			free_block_pointers[package_id][die_id] = free_block;
		} else {
			perform_gc(package_id, die_id, 0, event.get_start_time() + event.get_time_taken());
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
	Address free_block = find_free_unused_block(addr.package, addr.die);
	if (free_block.valid != NONE) {
		pointer = free_block;
	} else {
		perform_gc(addr.package, addr.die, 1, start_time);
	}
}

void Block_manager_parallel_wearwolf::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	if (event.get_id() == 255) {
		int i = 0;
		i++;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	reset_any_filled_pointers(event);
	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
	Wear_Level(event);
}

// must really improve logic in this class. Currently, mistakes are too easy if much GC happens at same time
void Block_manager_parallel_wearwolf::reset_any_filled_pointers(Event const& event) {
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (free_block_pointers[package_id][die_id].page >= BLOCK_SIZE) {
		free_block_pointers[package_id][die_id] = find_free_unused_block(package_id, die_id);
	}
	else if (wcrh_pointer.page >= BLOCK_SIZE) {
		wcrh_pointer = find_free_unused_block(package_id, die_id);;
	} else if (wcrc_pointer.page >= BLOCK_SIZE) {
		wcrc_pointer = find_free_unused_block(package_id, die_id);;
	}
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

pair<double, Address> Block_manager_parallel_wearwolf::write(Event const& write) const {
	pair<double, Address> result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		result.first = 1;
		return result;
	}

	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(write.get_logical_address());

	bool relevant_pointer_unavailable = false;

	if (w_hotness == WRITE_HOT) {
		result.second = get_free_die_with_shortest_IO_queue();
		if (result.second.valid == NONE) {
			result.first = 1;
			relevant_pointer_unavailable = true;
		} else {
			result.first = in_how_long_can_this_event_be_scheduled(result.second, write.get_current_time());
		}
	} else if (w_hotness == WRITE_COLD && r_hotness == READ_COLD) {
		if (wcrc_pointer.page >= BLOCK_SIZE) {
			result.first = 1;
			relevant_pointer_unavailable = true;
		} else {
			result.first = in_how_long_can_this_event_be_scheduled(wcrc_pointer, write.get_current_time());
			result.second = wcrc_pointer;
		}
	} else if (w_hotness == WRITE_COLD && r_hotness == READ_HOT) {
		if (wcrh_pointer.page >= BLOCK_SIZE) {
			result.first = 1;
			relevant_pointer_unavailable = true;
		} else {
			result.first = in_how_long_can_this_event_be_scheduled(wcrh_pointer, write.get_current_time());
			result.second = wcrh_pointer;
		}
	}

	if (write.is_garbage_collection_op() && relevant_pointer_unavailable) {
		if (wcrh_pointer.page < BLOCK_SIZE) {
			result.second = wcrh_pointer;
		} else if (wcrc_pointer.page < BLOCK_SIZE) {
			result.second = wcrc_pointer;
		}
		if (result.second.valid != NONE) {
			result.first = in_how_long_can_this_event_be_scheduled(result.second, write.get_current_time());
		}
		printf("MAKING A MISTAKE\n");
	}

	return result;
}

void Block_manager_parallel_wearwolf::register_read_outcome(Event const& event, enum status status){
	if (event.get_id() == 243) {
		int i = 0;
		i++;
	}
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Block_manager_parallel_wearwolf::check_if_should_trigger_more_GC(double start_time) {
	Block_manager_parent::check_if_should_trigger_more_GC(start_time);
	if (wcrh_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_HOT, start_time);
	}
	if (wcrc_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_COLD, start_time);
	}
}
