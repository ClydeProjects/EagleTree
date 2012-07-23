
#include "ssd.h"

using namespace ssd;

Block_manager_parallel_hot_cold_seperation::Block_manager_parallel_hot_cold_seperation(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parent(ssd, ftl, 2),
	  page_hotness_measurer()
{
	cold_pointer = find_free_unused_block(0, 0, 0);
}

Block_manager_parallel_hot_cold_seperation::~Block_manager_parallel_hot_cold_seperation(void) {}

void Block_manager_parallel_hot_cold_seperation::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_write_outcome(event, status);
	if (event.is_original_application_io()) {
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
	else if (block_address.compare(cold_pointer) == BLOCK) {
		cold_pointer.page = num_pages_written = cold_pointer.page + 1;
	}

	// there is still more room in this pointer, so no need to trigger GC
	if (num_pages_written < BLOCK_SIZE) {
		return;
	}

	// check if the pointer if full. If it is, find a free block for a new pointer, or trigger GC if there are no free blocks
	if (block_address.compare(free_block_pointers[package_id][die_id]) == BLOCK) {
		printf("hot pointer "); free_block_pointers[package_id][die_id].print(); printf(" is out of space\n");
		Address free_block = find_free_unused_block(package_id, die_id, 0);
		if (free_block.valid != NONE) {
			free_block_pointers[package_id][die_id] = free_block;
		} else {
			perform_gc(package_id, die_id, 0, event.get_start_time() + event.get_time_taken());
		}
	} else if (block_address.compare(cold_pointer) == BLOCK) {
		handle_cold_pointer_out_of_space(event.get_start_time() + event.get_time_taken());
	}
}

void Block_manager_parallel_hot_cold_seperation::handle_cold_pointer_out_of_space(double start_time) {
	Address free_block = find_free_unused_block_with_class(1, start_time);
	if (free_block.valid != NONE) {
		cold_pointer = free_block;
	} else {
		perform_gc(1, start_time);
	}
}

void Block_manager_parallel_hot_cold_seperation::register_erase_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == ERASE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_erase_outcome(event, status);
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (free_block_pointers[package_id][die_id].page >= BLOCK_SIZE) {
		free_block_pointers[package_id][die_id] = find_free_unused_block(package_id, die_id);
	}
	else if (cold_pointer.page >= BLOCK_SIZE) {
		cold_pointer = find_free_unused_block(package_id, die_id);
	}

	check_if_should_trigger_more_GC(event.get_start_time() + event.get_time_taken());
}

// ensures the pointer has at least 1 free page, and that the die is not busy (waiting for a read)
bool Block_manager_parallel_hot_cold_seperation::pointer_can_be_written_to(Address pointer) const {
	bool has_space = pointer.page < BLOCK_SIZE;
	bool non_busy = !ssd.getPackages()[pointer.package].getDies()[pointer.die].register_is_busy();
	return has_space && non_busy;
}


bool Block_manager_parallel_hot_cold_seperation::at_least_one_available_write_hot_pointer() const  {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			if (pointer_can_be_written_to(free_block_pointers[i][j])) {
				return true;
			}
		}
	}
	return false;
}

pair<double, Address> Block_manager_parallel_hot_cold_seperation::write(Event const& write) {
	pair<double, Address> result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		result.first = 1;
		return result;
	}

	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	bool relevant_pointer_unavailable = false;

	if (w_hotness == WRITE_HOT) {
		result.second = get_free_die_with_shortest_IO_queue();
		if (result.second.valid == NONE) {
			result.first = 1;
			relevant_pointer_unavailable = true;
		} else {
			result.first = in_how_long_can_this_event_be_scheduled(result.second, write.get_start_time() + write.get_time_taken());
		}
	} else if (w_hotness == WRITE_COLD) {
		if (cold_pointer.page >= BLOCK_SIZE) {
			result.first = 1;
			relevant_pointer_unavailable = true;
		} else {
			result.first = in_how_long_can_this_event_be_scheduled(cold_pointer, write.get_start_time() + write.get_time_taken());
			result.second = cold_pointer;
		}
	}

	if (write.is_garbage_collection_op() && relevant_pointer_unavailable) {
		assert(false);
	}

	return result;
}

void Block_manager_parallel_hot_cold_seperation::register_read_outcome(Event const& event, enum status status){
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Block_manager_parallel_hot_cold_seperation::check_if_should_trigger_more_GC(double start_time) {
	Block_manager_parent::check_if_should_trigger_more_GC(start_time);
	if (cold_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(start_time);
	}
}
