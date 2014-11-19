
#include "../ssd.h"

using namespace ssd;

Shortest_Queue_Hot_Cold_BM::Shortest_Queue_Hot_Cold_BM()
	: Block_manager_parent(1),
	  page_hotness_measurer(),
	  cold_pointer(find_free_unused_block(0))
{}

Shortest_Queue_Hot_Cold_BM::~Shortest_Queue_Hot_Cold_BM() {}

void Shortest_Queue_Hot_Cold_BM::register_write_outcome(Event const& event, enum status status) {
	Block_manager_parent::register_write_outcome(event, status);
	page_hotness_measurer.register_event(event);

	if (event.get_address().compare(cold_pointer) >= BLOCK) {
		cold_pointer.page = cold_pointer.page + 1;
		printf("cold write:   "); event.print();
		if (!has_free_pages(cold_pointer)) {
			handle_cold_pointer_out_of_space(event.get_current_time());
		}
	}
}

void Shortest_Queue_Hot_Cold_BM::handle_cold_pointer_out_of_space(double start_time) {
	//Address block = find_free_unused_block_with_class(1, start_time);
	//cold_pointer = has_free_pages(block) ? block : find_free_unused_block(start_time);
	cold_pointer = find_free_unused_block(OLD, start_time);
	if (!has_free_pages(cold_pointer)) {
		//schedule_gc(start_time);
	}
}

void Shortest_Queue_Hot_Cold_BM::register_erase_outcome(Event& event, enum status status) {
	Block_manager_parent::register_erase_outcome(event, status);
	Address addr = event.get_address();

	// TODO: Need better logic for this assignment. Easiest to remember some state.
	// when we trigger GC for a cold pointer, remember which block was chosen.
	if (!has_free_pages(free_block_pointers[addr.package][addr.die])) {
		free_block_pointers[addr.package][addr.die] = find_free_unused_block(addr.package, addr.die, YOUNG, event.get_current_time());
	}
	else if (!has_free_pages(cold_pointer)) {
		cold_pointer = find_free_unused_block(OLD, event.get_current_time());
	}

	check_if_should_trigger_more_GC(event);
}


Address Shortest_Queue_Hot_Cold_BM::choose_best_address(Event& write) {
	enum write_hotness w_hotness = page_hotness_measurer.get_write_hotness(write.get_logical_address());
	//w_hotness = WRITE_HOT;
	return w_hotness == WRITE_HOT ? get_free_block_pointer_with_shortest_IO_queue() : cold_pointer;
}

Address Shortest_Queue_Hot_Cold_BM::choose_any_address(Event const& write) {
	Address a = get_free_block_pointer_with_shortest_IO_queue();
	return has_free_pages(a) ? a : cold_pointer;
}

void Shortest_Queue_Hot_Cold_BM::register_read_command_outcome(Event const& event, enum status status){
	if (status == SUCCESS && !event.is_garbage_collection_op()) {
		page_hotness_measurer.register_event(event);
	}
}

void Shortest_Queue_Hot_Cold_BM::check_if_should_trigger_more_GC(Event const& event) {
	if (!has_free_pages(cold_pointer)) {
		handle_cold_pointer_out_of_space(event.get_current_time());
	}
}
