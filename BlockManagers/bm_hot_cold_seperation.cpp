
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


Address Block_manager_parallel_hot_cold_seperation::choose_best_address(Event const& write) {
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	return w_hotness == WRITE_HOT ? get_free_block_pointer_with_shortest_IO_queue() : cold_pointer;
}

Address Block_manager_parallel_hot_cold_seperation::choose_any_address() {
	Address a = Block_manager_parent::choose_any_address();
	return has_free_pages(a) ? a : cold_pointer;
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
