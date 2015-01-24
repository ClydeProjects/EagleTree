#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FAST::FAST(Ssd *ssd, Block_manager_parent* bm, Migrator* migrator) :
		FtlParent(ssd, bm),
		translation_table(NUMBER_OF_ADDRESSABLE_BLOCKS(), Address()),
		num_active_log_blocks(0),
		dial(0),
		NUM_LOG_BLOCKS(), // TODO find a good way of setting the max number of log blocks
		migrator(migrator),
		page_mapping(FtlImpl_Page(ssd, bm)),
		full_log_blocks(),
		queued_events(),
		gc_queue(),
		logical_dependencies()
{
	// The over-provisioned blocks serve as log blocks. We don't use all of them, though, because we need to keep
	// reserve free blocks for garbage-collection
	int num_over_prov_blocks = NUMBER_OF_ADDRESSABLE_BLOCKS() * (1 - OVER_PROVISIONING_FACTOR);
	NUM_LOG_BLOCKS = num_over_prov_blocks - BLOCK_SIZE * 3;
	if (GREED_SCALE > 0) {
		printf("Warning: the parameter GREED_SCALE must be set to 0 for FAST. We set it to 0 here on your behalf.\n");
	}
	GREED_SCALE = 0;
	IS_FTL_PAGE_MAPPING = false;
}

FAST::FAST() :
		FtlParent(),
		translation_table(),
		num_active_log_blocks(0),
		dial(0),
		NUM_LOG_BLOCKS(), // TODO find a good way of setting the max number of log blocks
		migrator(),
		page_mapping(),
		full_log_blocks(),
		queued_events(),
		gc_queue(),
		logical_dependencies()
{
	IS_FTL_PAGE_MAPPING = false;
}

FAST::~FAST(void)
{
	print();
	assert(queued_events.empty());
	assert(gc_queue.empty());
}

void FAST::read(Event *event)
{
	scheduler->schedule_event(event);
}

void FAST::register_read_completion(Event const& event, enum status result) {
	int block_id = event.get_logical_address() / BLOCK_SIZE;

	if (event.is_garbage_collection_op()) {
		queue<Event*>& q = gc_queue.at(block_id);
		Event* gc_write = q.front();
		q.pop();
		gc_write->set_start_time(event.get_current_time());
		write(gc_write);
		if (q.size() > 0) {
			Event* gc_read = q.front();
			set_read_address(*gc_read);
			q.pop();
			gc_read->set_start_time(event.get_current_time());
			scheduler->schedule_event(gc_read);
		}

	}
}

void FAST::schedule(Event* e) {
	int phys_block_id = e->get_address().get_block_id();
	if (queued_events.count(phys_block_id) == 0) {
		scheduler->schedule_event(e);
	}
	queue_up(e, e->get_address());
}

void FAST::queue_up(Event* e, Address const& lock) {
	int phys_block_id = lock.get_block_id();
	if (queued_events.count(phys_block_id) == 0) {
		queued_events[phys_block_id] = queue<Event*>();
	}
	else {
		queued_events.at(phys_block_id).push(e);
	}
}

// TODO: going to experience problems with writes canceling each other. Incrementing the block pointer should be elsewhere.
void FAST::write(Event *event)
{
	// find the block address
	int block_id = event->get_logical_address() / BLOCK_SIZE;
	Address& addr = translation_table[block_id];
	int page_id = event->get_logical_address() % BLOCK_SIZE;
	set_replace_address(*event);

	// Choose new address. Get one from block manager
	if (addr.valid == NONE) {
		Address new_addr = bm->find_free_unused_block(event->get_current_time());
		assert(new_addr.valid >= BLOCK);
		new_addr.valid = PAGE;
		event->set_address(new_addr);
		new_addr.page++;
		translation_table[block_id] = new_addr;
		schedule(event);
	}
	// The block is being garbage-collected.
	else if (gc_queue.count(block_id) == 1 && !event->is_garbage_collection_op()) {
		write_in_log_block(event);
	}
	// can write next page
	else if (page_id == addr.page) {
		event->set_address(addr);
		addr.page = page_id + 1;
		schedule(event);
	}
	// retain event
	else if (page_id > addr.page) {
		Address ideal_addr = addr;
		ideal_addr.page = page_id;
		queue_up(event, ideal_addr);
		//queued_events[ideal_addr.get_block_id()].push(event);
	}
	else {
		write_in_log_block(event);
	}
}

void FAST::write_in_log_block(Event* event) {
	int block_id = event->get_logical_address() / BLOCK_SIZE;
	// this block is mapped to an existing log block
	if (active_log_blocks_map.count(block_id) == 1 && active_log_blocks_map.at(block_id)->addr.page < BLOCK_SIZE) {
		log_block* log_block_id = active_log_blocks_map.at(block_id);
		Address& log_block_addr = log_block_id->addr;
		if (log_block_addr.get_block_id() == 1544) {
			int i = 0;
			i++;
		}
		int block_id_log = log_block_addr.get_block_id();
		event->set_address(log_block_addr);
		log_block_addr.page++;
		schedule(event);
	}
	// the log block is full.
	else if (active_log_blocks_map.count(block_id) == 1 /*&& active_log_blocks_map.at(block_id)->addr.page == BLOCK_SIZE */) {
		log_block* log_block_id = active_log_blocks_map.at(block_id);
		Address& log_block_addr = log_block_id->addr;
		int block_id_log = log_block_addr.get_block_id();
		//printf("block_id_log  %d\n");
		assert(queued_events.count(log_block_addr.get_block_id()) == 1);
		int size = queued_events.at(log_block_addr.get_block_id()).size();
		queue_up(event, log_block_addr);
		//queued_events[log_block_addr.get_block_id()].push(event);
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
			lb->num_blocks_mapped_inside.insert(block_id);
			active_log_blocks_map[block_id] = lb;
			num_active_log_blocks++;
			schedule(event);
		}
	}
	// We can't allocate any more log blocks, so we map the page into an existing block
	// The best strategy is to choose
	else {
		choose_existing_log_block(event);
	}

}

void FAST::choose_existing_log_block(Event* event) {
	int block_id = event->get_logical_address() / BLOCK_SIZE;
	map<int, log_block*>::iterator it = active_log_blocks_map.lower_bound(dial);
	for (; it != active_log_blocks_map.end(); it++) {
		int key = (*it).first;
		log_block* lb = (*it).second;
		if (lb->addr.page < BLOCK_SIZE - 1) {
			event->set_address(lb->addr);
			lb->addr.page++;
			lb->num_blocks_mapped_inside.insert(block_id);
			active_log_blocks_map[block_id] = lb;
			dial = (*it).first + 1;
			schedule(event);
			return;
		}
	}
	it = active_log_blocks_map.begin();
	for (; it != active_log_blocks_map.lower_bound(dial); it++) {
		int key  = (*it).first;
		log_block* lb = (*it).second;
		if (lb->addr.page < BLOCK_SIZE - 1) {
			event->set_address(lb->addr);
			lb->addr.page++;
			lb->num_blocks_mapped_inside.insert(block_id);
			active_log_blocks_map[block_id] = lb;
			dial = (*it).first + 1;
			if (event->get_id() == 32017) {
				int i = 0;
				i++;
			}
			schedule(event);
			return;
		}
	}
	assert(!event->is_garbage_collection_op());
	queued_events[-1].push(event);
}

void FAST::release_events_there_was_no_space_for() {
	if (queued_events.count(-1) == 0) {
		return;
	}
	queue<Event*>& q = queued_events.at(-1);
	int initial_size = q.size();
	for (int i = 0; i < initial_size; i++) {
		Event* e = q.front();
		if (e->get_application_io_id() == 77645) {
			int i =0;
			i++;
		}
		q.pop();
		write(e);
	}
	if (q.empty()) {
		queued_events.erase(-1);
	}
}

void FAST::unlock_block(Event const& event) {
	queue<Event*>& dependants = queued_events.at(event.get_address().get_block_id());
	if (dependants.empty()) {
		queued_events.erase(event.get_address().get_block_id());
	}
	else if (event.get_address().page == BLOCK_SIZE - 1) {
		while (!dependants.empty()) {
			Event* e = dependants.front();
			dependants.pop();
			if (e->get_event_type() == WRITE && e->get_address().valid == NONE) {
				write(e);
			}
			else {
				set_replace_address(*e);
				scheduler->schedule_event(e);
			}
		}
		queued_events.erase(event.get_address().get_block_id());
	}
	else {
		Event* e = dependants.front();
		dependants.pop();
		if (e->get_event_type() == WRITE && e->get_address().valid == NONE) {
			write(e);
		}
		else {
			set_replace_address(*e);
			scheduler->schedule_event(e);
		}
	}
}

void FAST::register_erase_completion(Event & event) {
	if (gc_queue.empty()) {
		release_events_there_was_no_space_for();
		consider_doing_garbage_collection(event.get_current_time());
	}
}

void FAST::register_write_completion(Event const& event, enum status result) {
	page_mapping.register_write_completion(event, result);

	int block_id = event.get_logical_address() / BLOCK_SIZE;
	Address& normal_block = translation_table[block_id];
	assert(normal_block.valid == PAGE);

	if (event.is_garbage_collection_op()) {
		queue<Event*>& q = gc_queue[block_id];
		if (q.empty() && event.get_address().page == BLOCK_SIZE - 1) {
			gc_queue.erase(block_id);
		}
		int size = gc_queue.size();
		if (gc_queue.empty()) {
			num_active_log_blocks--;
			release_events_there_was_no_space_for();
			consider_doing_garbage_collection(event.get_current_time());
		}
		unlock_block(event);
		return;
	}

	// page was written in normal block
	if (normal_block.compare(event.get_address()) >= BLOCK) {
		unlock_block(event);
		return;
	}

	// page was written in log block.
	assert(active_log_blocks_map.count(block_id) == 1);

	// Update page mapping in RAM
	log_block* lb = active_log_blocks_map.at(block_id);
	Address log_block_addr = lb->addr;
	assert(log_block_addr.compare(event.get_address()) >= BLOCK);

	// there is still more space in the log block
	if (event.get_address().page < BLOCK_SIZE - 1) {
		unlock_block(event);
		return;
	}

	// Remove active mapping of this log block
	//active_log_blocks_map.erase(block_id);
	for (auto i : lb->num_blocks_mapped_inside) {
		if (active_log_blocks_map.count(i) == 1 && active_log_blocks_map.at(i) == lb) {
			active_log_blocks_map.erase(i);
		}
	}

	// Log block is out of space. First check if a switch operation is possible.
	if (lb->num_blocks_mapped_inside.size() == 1) {
		// check if they are in order
		bool in_order = true;
		int first_logical_address = event.get_logical_address() / BLOCK_SIZE * BLOCK_SIZE;
		for (int i = 0; i < BLOCK_SIZE; i++) {
			Address logical_page_i = page_mapping.get_physical_address(first_logical_address + i);
			if (!(logical_page_i.compare(log_block_addr) >= BLOCK && logical_page_i.page == i)) {
				in_order = false;
			}
		}

		if (in_order) {
			translation_table[block_id] = log_block_addr;
			num_active_log_blocks--;
			delete lb;
		}
		else {
			full_log_blocks.push(lb);
		}
	}
	else {
		full_log_blocks.push(lb);
	}

	consider_doing_garbage_collection(event.get_current_time());
	unlock_block(event);
}


void FAST::consider_doing_garbage_collection(double time) {
	if (full_log_blocks.size() < 1 || gc_queue.size() > 0) {
		return;
	}

	log_block* lb = full_log_blocks.top();
	full_log_blocks.pop();

	set<long> logical_blocks_to_garbage_collect;
	Address it = lb->addr;
	it.page = 0;
	for (; it.page < BLOCK_SIZE; it.page++) {
		long page_logical_address = page_mapping.get_logical_address(it.get_linear_address());
		if (page_logical_address == UNDEFINED) {
			continue;
		}
		int logical_block_id = page_logical_address / BLOCK_SIZE;
		Address const& normal_address = translation_table[logical_block_id];
		if (normal_address.page == BLOCK_SIZE) {
			logical_blocks_to_garbage_collect.insert(logical_block_id);
		} else {
			// in this case, the log block cannot be deleted.
			consider_doing_garbage_collection(time);
			full_log_blocks.push(lb);
			return;
		}
	}

	if (logical_blocks_to_garbage_collect.empty()) {
		delete lb;
		consider_doing_garbage_collection(time);
		return;
	}

	for (auto b : logical_blocks_to_garbage_collect) {
		garbage_collect(b, lb, time);
	}

	delete lb;
}

// TODO: an optimization is possible here. When rewriting a block, the reads are on different blocks. Issue them in parallel
void FAST::garbage_collect(int block_id, log_block* log_block, double time) {
	Address new_addr = bm->find_free_unused_block(time);
	assert(queued_events.count(new_addr.get_block_id()) == 0);
	if (new_addr.valid < BLOCK) {
		printf("We ran out of free available log blocks. We are now stuck because we cannot complete gabrage collection.");
		printf("Try to increase over-provisioning.");
	}
	assert(new_addr.valid >= BLOCK);

	//translation_table[block_id].print();
	//new_addr.print();
	//printf("\t%d\n", block_id);
	long first_logical_addr = block_id * BLOCK_SIZE;
	gc_queue[block_id] = queue<Event*>();
	for (int i = 0; i < BLOCK_SIZE; i++) {
		long la = first_logical_addr + i;

		Event* read = new Event(READ, la, 1, time);
		read->set_garbage_collection_op(true);
		Event* write = new Event(WRITE, la, 1, time);
		write->set_garbage_collection_op(true);
		//write->set_address(new_addr);

		gc_queue[block_id].push(read);
		gc_queue[block_id].push(write);
	}

	Address& old_addr = translation_table[block_id];
	migrator->update_structures(old_addr, time);
	bm->subtract_from_available_for_new_writes(BLOCK_SIZE);
	translation_table[block_id] = new_addr;
	Event* first_read = gc_queue[block_id].front();
	set_read_address(*first_read);
	gc_queue[block_id].pop();
	scheduler->schedule_event(first_read);
}


void FAST::trim(Event *event)
{
	printf("We don't allow trims for FAST for now.\n");
	assert(false);
}

void FAST::register_trim_completion(Event & event) {
	page_mapping.register_trim_completion(event);
}

long FAST::get_logical_address(uint physical_address) const {
	return page_mapping.get_logical_address(physical_address);
}

Address FAST::get_physical_address(uint logical_address) const {
	return page_mapping.get_physical_address(logical_address);
}

void FAST::set_replace_address(Event& event) const {
	Address const& ra = page_mapping.get_physical_address(event.get_logical_address());
	event.set_replace_address(ra);
}

void FAST::set_read_address(Event& event) const {
	long la = event.get_logical_address();
	Address cur_block_addr = page_mapping.get_physical_address(event.get_logical_address());
	cur_block_addr.valid = PAGE;
	event.set_address(cur_block_addr);
}

// used for debugging
void FAST::print() const {
	for (auto locked_block : queued_events) {
		printf("block: %d\n", locked_block.first);
		queue<Event*>& q = locked_block.second;
		for (int i = 0; i < q.size(); i++) {
			Event* e = q.front();
			q.pop();
			printf("block: %d    logical address: %d\t", locked_block.first, e->get_logical_address());
			e->print();
		}
	}
}
