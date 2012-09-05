
#include "../ssd.h"

using namespace ssd;

Block_manager_parallel_wearwolf::Block_manager_parallel_wearwolf(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parent(ssd, ftl, 1),
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
}

void Block_manager_parallel_wearwolf::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_write_outcome(event, status);
	page_hotness_measurer.register_event(event);

	// Increment block pointer
	Address block_address = Address(event.get_address().get_linear_address(), BLOCK);

	if (block_address.compare(wcrh_pointer) == BLOCK && has_free_pages(wcrh_pointer)) {
		wcrh_pointer.page = wcrh_pointer.page + 1;
		if (wcrh_pointer.page > BLOCK_SIZE) {
			event.print();
		}
		assert(wcrh_pointer.page <= BLOCK_SIZE);
	} else if (block_address.compare(wcrc_pointer) == BLOCK && has_free_pages(wcrc_pointer)) {
		wcrc_pointer.page = wcrc_pointer.page + 1;
		assert(wcrc_pointer.page <= BLOCK_SIZE);
	}

	if (!has_free_pages(wcrh_pointer)) {
		handle_cold_pointer_out_of_space(READ_HOT, event.get_current_time());
	}
	else if (!has_free_pages(wcrc_pointer)) {
		handle_cold_pointer_out_of_space(READ_COLD, event.get_current_time());
	}
}

void Block_manager_parallel_wearwolf::handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time) {
	Address addr = page_hotness_measurer.get_best_target_die_for_WC(rh);
	Address& pointer = rh == READ_COLD ? wcrc_pointer : wcrh_pointer;
	Address free_block = find_free_unused_block(addr.package, addr.die, start_time);
	if (free_block.valid != NONE) {
		pointer = free_block;
	} else {
		//perform_gc(addr.package, addr.die, 1, start_time);
		schedule_gc(start_time, addr.package, addr.die, 0);
	}
}

void Block_manager_parallel_wearwolf::register_erase_outcome(Event const& event, enum status status) {
	Block_manager_parent::register_erase_outcome(event, status);
	reset_any_filled_pointers(event);
	check_if_should_trigger_more_GC(event.get_current_time());
}

// must really improve logic in this class. Currently, mistakes are too easy if much GC happens at same time
void Block_manager_parallel_wearwolf::reset_any_filled_pointers(Event const& event) {
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (!has_free_pages(free_block_pointers[package_id][die_id])) {
		free_block_pointers[package_id][die_id] = find_free_unused_block(package_id, die_id, event.get_current_time());
	}
	else if (!has_free_pages(wcrh_pointer)) {
		wcrh_pointer = find_free_unused_block(package_id, die_id, event.get_current_time());
	} else if (!has_free_pages(wcrc_pointer)) {
		wcrc_pointer = find_free_unused_block(package_id, die_id, event.get_current_time() );
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

Address Block_manager_parallel_wearwolf::write(Event const& write) {
	Address result;
	bool can_write = Block_manager_parent::can_write(write);
	if (!can_write) {
		return result;
	}

	if (write.get_id() == 17790 && write.get_bus_wait_time() > 934) {
		write.print();
	}

	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(write.get_logical_address());

	if (w_hotness == WRITE_HOT) {
		result = get_free_block_pointer_with_shortest_IO_queue();
	} else if (w_hotness == WRITE_COLD && r_hotness == READ_COLD) {
		result = wcrc_pointer;
	} else if (w_hotness == WRITE_COLD && r_hotness == READ_HOT) {
		result = wcrh_pointer;
	}
	if (result.valid == PAGE && result.page < BLOCK_SIZE) {
		return result;
	}

	if (!write.is_garbage_collection_op() && how_many_gc_operations_are_scheduled() == 0) {
		schedule_gc(write.get_current_time());
	}

	if (write.is_garbage_collection_op() || how_many_gc_operations_are_scheduled() == 0) {

		if (PRINT_LEVEL > 1) {
			printf("Trying to migrate a write %s page, but could not find a relevant pointer.\n", w_hotness == WRITE_COLD ? "cold" : "hot");
		}

		if (wcrh_pointer.page < BLOCK_SIZE) {
			result = wcrh_pointer;
		} else if (wcrc_pointer.page < BLOCK_SIZE) {
			result = wcrc_pointer;
		} else if (w_hotness == WRITE_COLD) {
			result = get_free_block_pointer_with_shortest_IO_queue();
		}
	}
	return result;
}

void Block_manager_parallel_wearwolf::register_read_outcome(Event const& event, enum status status){
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Block_manager_parallel_wearwolf::check_if_should_trigger_more_GC(double start_time) {
	if (wcrh_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_HOT, start_time);
	}
	if (wcrc_pointer.page >= BLOCK_SIZE) {
		handle_cold_pointer_out_of_space(READ_COLD, start_time);
	}
}
