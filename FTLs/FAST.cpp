#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FAST::FAST(Ssd *ssd, Block_manager_parent* bm) :
		translation_table(NUMBER_OF_ADDRESSABLE_BLOCKS(), Address()),
		num_active_log_blocks(0),
		bm(bm),
		dial(0),
		NUM_LOG_BLOCKS(NUMBER_OF_ADDRESSABLE_BLOCKS() * (1 - OVER_PROVISIONING_FACTOR) - 1)
{
	GREED_SCALE = 0;
}

FAST::FAST() :
		NUM_LOG_BLOCKS(NUMBER_OF_ADDRESSABLE_BLOCKS() * (1 - OVER_PROVISIONING_FACTOR) - 1)
{}

FAST::~FAST(void)
{}

void FAST::read(Event *event)
{

}

void FAST::register_read_completion(Event const& event, enum status result) {
}

void FAST::schedule(Event* e) {
	int phys_block_id = e->get_address().get_block_id();
	if (queued_events.count(phys_block_id) == 0) {
		queued_events[phys_block_id] = queue<Event*>();
		scheduler->schedule_event(e);
	}
	else {
		queued_events[phys_block_id].push(e);
	}
}

// TODO: going to experience problems with writes canceling each other. Incrementing the block pointer should be elsewhere.
void FAST::write(Event *event)
{
	if (event->get_id() == 25593) {
		int i = 0;
		i++;
	}

	if (event->get_start_time() >= 0 && event->get_logical_address() == 33) {
		int i = 0;
		i++;
	}

	if (event->get_start_time() >= 0 && event->get_logical_address() == 34) {
		int i = 0;
		i++;
	}

	// find the block address
	int block_id = event->get_logical_address() / BLOCK_SIZE;
	Address& addr = translation_table[block_id];
	int page_id = event->get_logical_address() % BLOCK_SIZE;

	// Choose new address. Get one from block manager
	if (addr.valid == NONE) {
		Address new_addr = bm->find_free_unused_block(event->get_current_time());
		assert(new_addr.valid >= BLOCK);
		new_addr.valid = PAGE;
		event->set_address(new_addr);
		new_addr.page++;
		translation_table[block_id] = new_addr;
	}
	// can write next page
	else if (page_id == addr.page) {
		event->set_address(addr);
		addr.page = page_id + 1;
	}
	// retain event
	else if (page_id > addr.page) {
		Address ideal_addr = addr;
		ideal_addr.page = page_id;
		queued_events[ideal_addr.get_block_id()].push(event);
		return;
	}
	// this block is mapped to an existing log block
	else if (active_log_blocks_map.count(block_id) == 1) {
		log_block* log_block_id = active_log_blocks_map[block_id];
		Address& log_block_addr = log_block_id->addr;
		event->set_address(log_block_addr);
		log_block_addr.page++;
		// try to exploit sequential optimization
		/*if (log_block_id->num_blocks_mapped_inside.size() == 1) {
			if (log_block_addr.page >= page_id) {
				event->set_address(log_block_addr);
				log_block_addr.page++;
			} else {
				Address ideal_addr = log_block_addr;
				ideal_addr.page = page_id;
				event->set_address(log_block_addr);
				queued_events[ideal_addr.get_block_id()][event->get_logical_address()].push(event);
				return;
			}
		}
		else {
			event->set_address(log_block_addr);
			log_block_addr.page++;
		}*/

	}
	// While we have not exceeded the allotted number of log blocks, just get a new log block for the write
	else if (num_active_log_blocks < NUM_LOG_BLOCKS) {
		Address new_addr = bm->find_free_unused_block(event->get_current_time());
		if (new_addr.valid == NONE) {
			choose_existing_log_block(event);
		}
		else {
			new_addr.valid = PAGE;
			event->set_address(new_addr);
			new_addr.page++;
			log_block* lb = new log_block(new_addr);
			lb->num_blocks_mapped_inside.push_back(block_id);
			active_log_blocks_map[block_id] = lb;
			num_active_log_blocks++;
		}
	}
	// We can't allocate any more log blocks, so we map the page into an existing block
	// The best strategy is to choose
	else {
		choose_existing_log_block(event);
	}
	schedule(event);
}

void FAST::choose_existing_log_block(Event* event) {
	int block_id = event->get_logical_address() / BLOCK_SIZE;
	map<int, log_block*>::iterator it = active_log_blocks_map.lower_bound(dial);
	for (; it != active_log_blocks_map.end(); it++) {
		log_block* lb = (*it).second;
		if (lb->addr.page < BLOCK_SIZE - 1) {
			event->set_address(lb->addr);
			lb->addr.page++;
			lb->num_blocks_mapped_inside.push_back(block_id);
			active_log_blocks_map[block_id] = lb;
			dial = (*it).first + 1;
			return;
		}
	}
	it = active_log_blocks_map.begin();
	for (; it != active_log_blocks_map.lower_bound(dial); it++) {
		log_block* lb = (*it).second;
		if (lb->addr.page < BLOCK_SIZE - 1) {
			event->set_address(lb->addr);
			lb->addr.page++;
			lb->num_blocks_mapped_inside.push_back(block_id);
			active_log_blocks_map[block_id] = lb;
			dial = (*it).first + 1;
			return;
		}
	}


}

void FAST::register_write_completion(Event const& event, enum status result) {

	if (event.get_start_time() >= 0 && event.get_logical_address() == 32) {
		int i = 0;
		i++;
	}

	if (event.get_start_time() >= 0 && event.get_logical_address() == 33) {
		int i = 0;
		i++;
	}

	if (event.get_start_time() >= 0 && event.get_logical_address() == 34) {
		int i = 0;
		i++;
	}

	int block_id = event.get_logical_address() / BLOCK_SIZE;
	Address& normal_block = translation_table[block_id];
	assert(normal_block.valid == PAGE);

	// If the block to which we are writing is still not full, see if there are retained events dependent on the event that just finished

	queue<Event*>& dependants = queued_events.at(event.get_address().get_block_id());


	if (dependants.empty()) {
		queued_events.erase(event.get_address().get_block_id());
	}
	else {
		Event* e = dependants.front();
		dependants.pop();
		scheduler->schedule_event(e);
	}

	/*if (it == dependants.end()) {
		if (dependants.begin() == dependants.end()) {
			assert(dependants.begin() == dependants.end());
			queued_events.erase(event.get_address().get_block_id());
		}
		else {
			map<int, queue<Event*> >::iterator i = dependants.begin();
			while (i != dependants.end()) {
				Event* e = (*i).second.front();
				(*i).second.pop();
				scheduler->schedule_event(e);
				if ((*i).second.empty()) {
					dependants.erase(i++);
				} else {
					++i;
				}
			}
		}
	}
	else {
		int logical_address_of_next = (*it).first;
		queue<Event*> how_many_waiting_Events = (*it).second;
		Event* retained_event = how_many_waiting_Events.front();
		how_many_waiting_Events.pop();
		if (how_many_waiting_Events.size() == 0) {
			dependants.erase(logical_address_of_next);
		}
		scheduler->schedule_event(retained_event);
	}*/



	// page was written in normal block
	if (normal_block.compare(event.get_address()) >= BLOCK) {
		return;
	}

	// page was written in log block
	assert(active_log_blocks_map.count(block_id) == 1);
	log_block* lb = active_log_blocks_map[block_id];
	Address& log_block_addr = lb->addr;
	assert(log_block_addr.compare(event.get_address()) >= BLOCK);

	// there is still more space in the log block
	if (event.get_address().page < BLOCK_SIZE - 1) {
		return;
	}

	// Log block is out of space. First check if a switch operation is possible.
	if (lb->num_blocks_mapped_inside.size() == 1) {
		// check if they are in order
		bool in_order = true;
		int first_logical_address = (event.get_logical_address() / NUMBER_OF_ADDRESSABLE_BLOCKS()) * NUMBER_OF_ADDRESSABLE_BLOCKS();
		for (int i = 0; i < BLOCK_SIZE; i++) {
			Address logical_page_i = logical_addresses_to_pages_in_log_blocks[first_logical_address + i];
			if (!(logical_page_i.compare(log_block_addr) >= BLOCK && logical_page_i.page == i)) {
				in_order = false;
			}
		}

		if (in_order) {
			active_log_blocks_map.erase(block_id);
			delete lb;
			translation_table[block_id] = log_block_addr;

			// TODO make sure that erase actually takes place elsewhere
		}
	}

	// trigger GC
	// allocate all blocks we need. Change addresses in translation table.
	for (int i = 0; i < lb->num_blocks_mapped_inside.size(); i++) {
		int cur_block_id = lb->num_blocks_mapped_inside[i];
		Address& cur_block_addr = translation_table[cur_block_id];



	}

}


void FAST::trim(Event *event)
{

}

void FAST::register_trim_completion(Event & event) {
}

long FAST::get_logical_address(uint physical_address) const {
	return 0;
}

Address FAST::get_physical_address(uint logical_address) const {
	return Address();
}

void FAST::set_replace_address(Event& event) const {
	int block_id = event.get_logical_address() / NUMBER_OF_ADDRESSABLE_BLOCKS();
	Address const& addr = translation_table[block_id];
	int page_id = event.get_logical_address() % BLOCK_SIZE;

	// set replace address for the new write
	if (logical_addresses_to_pages_in_log_blocks.count(event.get_logical_address()) == 1) {
		event.set_replace_address( logical_addresses_to_pages_in_log_blocks.at(event.get_logical_address()) );
	}
	else if (addr.page == BLOCK_SIZE - 1) {
		Address copy = addr;
		copy.page = page_id;
		event.set_replace_address(copy);
	}
}

void FAST::set_read_address(Event& event) const {

}

// used for debugging
void FAST::print() const {
	for (auto locked_block : queued_events) {
		queue<Event*> q = locked_block.second;
		for (int i = 0; i < q.size(); i++) {
			Event* e = q.front();
			q.pop();
			printf("block: %d    logical address: %d\t", locked_block.first, e->get_logical_address());
			e->print();
		}
	}
}
