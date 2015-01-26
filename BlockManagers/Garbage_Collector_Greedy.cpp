#include "../ssd.h"
#include "../block_management.h"
using namespace ssd;

Garbage_Collector_Greedy::Garbage_Collector_Greedy()
	:  Garbage_Collector(),
	   gc_candidates(SSD_SIZE, vector<set<long> >(PACKAGE_SIZE, set<long>()))
{}

Garbage_Collector_Greedy::Garbage_Collector_Greedy(Ssd* ssd, Block_manager_parent* bm)
	:  Garbage_Collector(ssd, bm),
	   gc_candidates(SSD_SIZE, vector<set<long> >(PACKAGE_SIZE, set<long>()))
{}

void Garbage_Collector_Greedy::commit_choice_of_victim(Address const& phys_address, double time) {
	gc_candidates[phys_address.package][phys_address.die].erase(phys_address.get_linear_address());
}

vector<long> Garbage_Collector_Greedy::get_relevant_gc_candidates(int package_id, int die_id, int klass) const {
	vector<long > candidates;
	int package = package_id == -1 ? 0 : package_id;
	int num_packages = package_id == -1 ? SSD_SIZE : package_id + 1;
	for (; package < num_packages; package++) {
		int die = die_id == -1 ? 0 : die_id;
		int num_dies = die_id == -1 ? PACKAGE_SIZE : die_id + 1;
		for (; die < num_dies; die++) {
			for (auto i : gc_candidates[package][die]) {
				candidates.push_back(i);
			}
		}
	}
	return candidates;
}

Block* Garbage_Collector_Greedy::choose_gc_victim(int package_id, int die_id, int klass) const {
	vector<long> candidates = get_relevant_gc_candidates(package_id, die_id, klass);
	uint min_valid_pages = BLOCK_SIZE;
	Block* best_block = NULL;
	for (auto physical_address : candidates) {
		Address a = Address(physical_address, BLOCK);
		Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
		if (block->get_pages_valid() < min_valid_pages && (block->get_state() == ACTIVE || block->get_state() == INACTIVE)) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}

	return best_block;
}

void Garbage_Collector_Greedy::register_event_completion(Event const& event) {
	if (event.get_event_type() != WRITE) {
		return;
	}
	Address ra = event.get_replace_address();
	if (ra.valid == NONE) {
		return;
	}
	ra.valid = BLOCK;
	ra.page = 0;
	if (PRINT_LEVEL > 1) {
		//printf("Inserting as GC candidate: %ld ", ra.get_linear_address()); ra.print(); printf(" with age_class %d and valid blocks: %d\n", num_live_pages);
	}
	gc_candidates[ra.package][ra.die].insert(ra.get_linear_address());
	if (gc_candidates[ra.package][ra.die].size() == 1) {
		bm->check_if_should_trigger_more_GC(event);
	}
}
