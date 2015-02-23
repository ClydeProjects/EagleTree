#include "../ssd.h"
#include "../block_management.h"
using namespace ssd;

Garbage_Collector_LRU::Garbage_Collector_LRU()
	:  Garbage_Collector(),
	   gc_candidates(SSD_SIZE, vector<queue<int> >(PACKAGE_SIZE))
{}

Garbage_Collector_LRU::Garbage_Collector_LRU(Ssd* ssd, Block_manager_parent* bm)
	:  Garbage_Collector(ssd, bm),
	   gc_candidates(SSD_SIZE, vector<queue<int> >(PACKAGE_SIZE))
{}

void Garbage_Collector_LRU::register_event_completion(Event const& event) {
	if (event.get_event_type() != WRITE) {
		return;
	}
	Address const& a = event.get_address();
	if (a.page == BLOCK_SIZE - 1) {
		/*printf("push ");
		a.print();
		printf("\n");*/
		gc_candidates[a.package][a.die].push(a.block);
	}
}

void Garbage_Collector_LRU::commit_choice_of_victim(Address const& phys_address, double time) {
	int package = phys_address.package;
	int die = phys_address.die;
	assert(phys_address.block == gc_candidates[package][die].front());
	gc_candidates[package][die].pop();
}

Block* Garbage_Collector_LRU::choose_gc_victim(int package_id, int die_id, int klass) const {
	if (package_id == UNDEFINED) {
		package_id = rand() % SSD_SIZE;
	}
	if (die_id == UNDEFINED) {
		die_id = rand() % PACKAGE_SIZE;
	}

	Block* block = NULL;
	Address a = Address(0, BLOCK);
	a.package = package_id;
	a.die = die_id;
	assert(gc_candidates[package_id][die_id].size() > 0);
	a.block = gc_candidates[package_id][die_id].front();
	block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	assert(block->get_state() != PARTIALLY_FREE && block->get_state() != FREE);
	return block;
}
