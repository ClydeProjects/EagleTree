
#include "../ssd.h"

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
	page_hotness_measurer.register_event(event);

	// Increment block pointer
	Address block_address = Address(event.get_address().get_linear_address(), BLOCK);
	if (block_address.compare(cold_pointer) == BLOCK) {
		cold_pointer.page = cold_pointer.page + 1;
	}

	if (has_free_pages(cold_pointer)) {
		handle_cold_pointer_out_of_space(event.get_current_time());
	}
}

void Block_manager_parallel_hot_cold_seperation::handle_cold_pointer_out_of_space(double start_time) {
	Address free_block = find_free_unused_block_with_class(1, start_time);
	if (free_block.valid != NONE) {
		cold_pointer = free_block;
	} else {
		schedule_gc(start_time, -1, -1, 1);
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

	check_if_should_trigger_more_GC(event.get_current_time());
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

Address Block_manager_parallel_hot_cold_seperation::write(Event const& write) {
	Address result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		return result;
	}

	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	bool relevant_pointer_unavailable = false;

	if (w_hotness == WRITE_HOT) {
		result = get_free_block_pointer_with_shortest_IO_queue();
	} else if (w_hotness == WRITE_COLD) {
		result = cold_pointer;
	}
	if (result.valid == PAGE && result.page < BLOCK_SIZE) {
		return result;
	}

	if (!write.is_garbage_collection_op() && how_many_gc_operations_are_scheduled() == 0) {
		schedule_gc(write.get_current_time());
	}

	if (write.is_garbage_collection_op() || how_many_gc_operations_are_scheduled() == 0) {

		if (cold_pointer.page < BLOCK_SIZE) {
			result = cold_pointer;
		} else if (w_hotness == WRITE_COLD) {
			result = get_free_block_pointer_with_shortest_IO_queue();
		}

		if (PRINT_LEVEL > 1) {
			printf("Trying to migrate a write %s page, but could not find a relevant pointer.\n", w_hotness == WRITE_COLD ? "cold" : "hot");
		}
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
