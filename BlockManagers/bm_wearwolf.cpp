
#include "../ssd.h"

using namespace ssd;

Wearwolf::Wearwolf(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parent(ssd, ftl, 4),
	  page_hotness_measurer()
{
	wcrh_pointer = find_free_unused_block(0, 0, YOUNG, 0);
	if (SSD_SIZE > 1) {
		wcrc_pointer = find_free_unused_block(1, 0, YOUNG, 0);
	} else if (PACKAGE_SIZE > 1) {
		wcrc_pointer = find_free_unused_block(0, 1, YOUNG, 0);
	} else {
		wcrc_pointer = find_free_unused_block(0, 0, YOUNG, 0);
	}
}

void Wearwolf::register_write_outcome(Event const& event, enum status status) {
	assert(event.get_event_type() == WRITE || event.get_event_type() == COPY_BACK);
	if (status == FAILURE) {
		return;
	}
	Block_manager_parent::register_write_outcome(event, status);
	page_hotness_measurer.register_event(event);

	if (event.get_address().compare(wcrh_pointer) >= BLOCK) {
		wcrh_pointer.page = wcrh_pointer.page + 1;
	} else if (event.get_address().compare(wcrc_pointer) >= BLOCK) {
		wcrc_pointer.page = wcrc_pointer.page + 1;
	}

	if (!has_free_pages(wcrh_pointer)) {
		handle_cold_pointer_out_of_space(READ_HOT, event.get_current_time());
	}
	else if (!has_free_pages(wcrc_pointer)) {
		handle_cold_pointer_out_of_space(READ_COLD, event.get_current_time());
	}
}

void Wearwolf::handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time) {
	Address addr = page_hotness_measurer.get_best_target_die_for_WC(rh);
	Address& pointer = rh == READ_COLD ? wcrc_pointer : wcrh_pointer;
	Address block = find_free_unused_block(addr.package, addr.die, OLD, start_time);
	if (has_free_pages(block)) {
		pointer = block;
	} else {
		//perform_gc(addr.package, addr.die, 1, start_time);
		schedule_gc(start_time, addr.package, addr.die, -1, -1);
	}
}

void Wearwolf::register_erase_outcome(Event const& event, enum status status) {
	Block_manager_parent::register_erase_outcome(event, status);
	reset_any_filled_pointers(event);
	check_if_should_trigger_more_GC(event.get_current_time());
}

// must really improve logic in this class. Currently, mistakes are too easy if much GC happens at same time
void Wearwolf::reset_any_filled_pointers(Event const& event) {
	uint package_id = event.get_address().package;
	uint die_id = event.get_address().die;

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (!has_free_pages(free_block_pointers[package_id][die_id])) {
		free_block_pointers[package_id][die_id] = find_free_unused_block(package_id, die_id, YOUNG, event.get_current_time());
	}
	else if (!has_free_pages(wcrh_pointer)) {
		wcrh_pointer = find_free_unused_block(package_id, die_id, OLD, event.get_current_time());
	} else if (!has_free_pages(wcrc_pointer)) {
		wcrc_pointer = find_free_unused_block(package_id, die_id, OLD, event.get_current_time() );
	}
}


Address Wearwolf::choose_best_address(Event const& write) {
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	enum read_hotness r_hotness = page_hotness_measurer.get_read_hotness(write.get_logical_address());

	if (w_hotness == WRITE_HOT) {
		return get_free_block_pointer_with_shortest_IO_queue();
	} else if (w_hotness == WRITE_COLD && r_hotness == READ_COLD) {
		return wcrc_pointer;
	} else /* if (w_hotness == WRITE_COLD && r_hotness == READ_HOT) */ {
		return wcrh_pointer;
	}
}

Address Wearwolf::choose_any_address() {
	Address a = get_free_block_pointer_with_shortest_IO_queue();
	return has_free_pages(a) ? a : has_free_pages(wcrh_pointer) ? wcrh_pointer : wcrc_pointer;
}

void Wearwolf::register_read_outcome(Event const& event, enum status status){
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Wearwolf::check_if_should_trigger_more_GC(double start_time) {
	if (!has_free_pages(wcrh_pointer)) {
		handle_cold_pointer_out_of_space(READ_HOT, start_time);
	}
	if (!has_free_pages(wcrc_pointer)) {
		handle_cold_pointer_out_of_space(READ_COLD, start_time);
	}
}
