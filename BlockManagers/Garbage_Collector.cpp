#include "../ssd.h"
using namespace ssd;

Garbage_Collector::Garbage_Collector(Ssd* ssd, Block_manager_parent* bm, int num_age_classes)
	:  gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(num_age_classes, set<long>()))),
	   ssd(ssd),
	   bm(bm),
	   num_age_classes(num_age_classes)
{}

void Garbage_Collector::remove_as_gc_candidate(Address const& phys_address) {
	for (int i = 0; i < num_age_classes; i++) {
		gc_candidates[phys_address.package][phys_address.die][i].erase(phys_address.get_linear_address());
	}
}

vector<long> Garbage_Collector::get_relevant_gc_candidates(int package_id, int die_id, int klass) const {
	vector<long > candidates;
	int package = package_id == -1 ? 0 : package_id;
	int num_packages = package_id == -1 ? SSD_SIZE : package_id + 1;
	for (; package < num_packages; package++) {
		int die = die_id == -1 ? 0 : die_id;
		int num_dies = die_id == -1 ? PACKAGE_SIZE : die_id + 1;
		for (; die < num_dies; die++) {
			int age_class = klass == -1 ? 0 : klass;
			int num_classes = klass == -1 ? num_age_classes : klass + 1;
			for (; age_class < num_classes; age_class++) {
				set<long>::iterator iter = gc_candidates[package][die][age_class].begin();
				for (; iter != gc_candidates[package][die][age_class].end(); ++iter) {
					long g = *iter;
					candidates.push_back(*iter);
				}
			}
		}
	}
	if (candidates.size() == 0 && klass != -1) {
		candidates = get_relevant_gc_candidates(package_id, die_id, -1);
	}
	return candidates;
}

Block* Garbage_Collector::choose_gc_victim(int package_id, int die_id, int klass) const {
	vector<long> candidates = get_relevant_gc_candidates(package_id, die_id, klass);
	uint min_valid_pages = BLOCK_SIZE;
	Block* best_block = NULL;
	for (uint i = 0; i < candidates.size(); i++) {
		long physical_address = candidates[i];
		Address a = Address(physical_address, BLOCK);
		Block* block = &ssd->getPackages()[a.package].getDies()[a.die].getPlanes()[a.plane].getBlocks()[a.block];
		if (block->get_pages_valid() < min_valid_pages && block->get_state() == ACTIVE) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}
	return best_block;
}

void Garbage_Collector::register_erase_completion(Event const& event, uint age_class) {
	Address ra = event.get_replace_address();
	ra.valid = BLOCK;
	ra.page = 0;
	remove_as_gc_candidate(ra);
	if (PRINT_LEVEL > 1) {
		//printf("Inserting as GC candidate: %ld ", ra.get_linear_address()); ra.print(); printf(" with age_class %d and valid blocks: %d\n", age_class, block.get_pages_valid());
	}
	gc_candidates[ra.package][ra.die][age_class].insert(ra.get_linear_address());
	if (gc_candidates[ra.package][ra.die][age_class].size() == 1) {
		bm->check_if_should_trigger_more_GC(event.get_current_time());
	}
}
