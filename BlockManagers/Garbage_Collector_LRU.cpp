#include "../ssd.h"
#include "../block_management.h"
using namespace ssd;

Garbage_Collector_LRU::Garbage_Collector_LRU()
	:  Garbage_Collector(),
	   gc_candidates(SSD_SIZE, vector<int>(PACKAGE_SIZE, PLANE_SIZE))
{}

Garbage_Collector_LRU::Garbage_Collector_LRU(Ssd* ssd, Block_manager_parent* bm)
	:  Garbage_Collector(ssd, bm),
	   gc_candidates(SSD_SIZE, vector<int>(PACKAGE_SIZE, PLANE_SIZE))
{}

void Garbage_Collector_LRU::commit_choice_of_victim(Address const& phys_address, double time) {
	int package = phys_address.package;
	int die = phys_address.die;
	gc_candidates[package][die] = phys_address.block - 1;
	if (gc_candidates[package][die] == 0) {
		gc_candidates[package][die] = PLANE_SIZE;
	}
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
	a.block = gc_candidates[package_id][die_id];
	//StateVisualiser::print_page_status();
	do {
		block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
		a.block--;
		if (a.block == 0) {
			a.block = PLANE_SIZE;
		}
	} while (block == NULL || block->get_state() == PARTIALLY_FREE || block->get_state() == FREE);

	//a.print();
	//printf("\t%d   %d\n", gc_candidates[package_id][die_id], block->get_pages_valid());
	/*if (block->get_pages_valid() > 60) {
		StateVisualiser::print_page_status();

	}*/

	//printf("\n");
	return block;
}
