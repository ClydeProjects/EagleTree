#include "../ssd.h"
#include "../block_management.h"
using namespace ssd;

Garbage_Collector::Garbage_Collector()
	:  gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(1, set<long>()))),
	   //blocks_per_lun_with_min_live_pages(SSD_SIZE, vector<pair<Address, int> >(PACKAGE_SIZE)),
	   ssd(NULL),
	   bm(NULL),
	   num_age_classes(1)
{}

Garbage_Collector::Garbage_Collector(Ssd* ssd, Block_manager_parent* bm)
	:  gc_candidates(SSD_SIZE, vector<vector<set<long> > >(PACKAGE_SIZE, vector<set<long> >(bm->get_num_age_classes(), set<long>()))),
	   //blocks_per_lun_with_min_live_pages(SSD_SIZE, vector<pair<Address, int> >(PACKAGE_SIZE)),
	   ssd(ssd),
	   bm(bm),
	   num_age_classes(bm->get_num_age_classes())
{}

void Garbage_Collector::remove_as_gc_candidate(Address const& phys_address) {
	//phys_address.print();
	for (int i = 0; i < num_age_classes; i++) {
		gc_candidates[phys_address.package][phys_address.die][i].erase(phys_address.get_linear_address());
	}
	/*pair<Address, int>& map = blocks_per_lun_with_min_live_pages[phys_address.package][phys_address.die];
	if (map.first.valid == BLOCK && map.first.compare(phys_address) == BLOCK) {
		Block* b = choose_gc_victim(phys_address.package, phys_address.die, -1);
		map.first = b != NULL ? Address(b->get_physical_address(), BLOCK) : Address();
		map.second = b != NULL ? b->get_pages_valid() : 0;
	}*/
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
				for (auto i :  gc_candidates[package][die][age_class]) {
					candidates.push_back(i);
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
	for (auto physical_address : candidates) {
		Address a = Address(physical_address, BLOCK);
		Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
		if (block->get_pages_valid() < min_valid_pages && block->get_state() == ACTIVE) {
			min_valid_pages = block->get_pages_valid();
			best_block = block;
			assert(min_valid_pages < BLOCK_SIZE);
		}
	}
	return best_block;
}

void Garbage_Collector::register_trim(Event const& event, uint age_class, int num_live_pages) {
	Address ra = event.get_replace_address();
	assert(ra.valid != NONE);
	ra.valid = BLOCK;
	ra.page = 0;
	remove_as_gc_candidate(ra);
	if (PRINT_LEVEL > 1) {
		//printf("Inserting as GC candidate: %ld ", ra.get_linear_address()); ra.print(); printf(" with age_class %d and valid blocks: %d\n", num_live_pages);
	}
	gc_candidates[ra.package][ra.die][age_class].insert(ra.get_linear_address());
	if (gc_candidates[ra.package][ra.die][age_class].size() == 1) {
		bm->check_if_should_trigger_more_GC(event.get_current_time());
	}
	/*pair<Address, int>& map = blocks_per_lun_with_min_live_pages[ra.package][ra.die];
	if (map.first.valid != BLOCK || map.first.compare(ra) == BLOCK || map.second > num_live_pages) {
		map.first = ra;
		map.second = num_live_pages;
	}*/
}

/*double Garbage_Collector::get_average_live_pages_per_best_candidate() const {
	int total_live_pages = 0;
	int num_blocks_considered = 0;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			pair<Address, int> const& map = blocks_per_lun_with_min_live_pages[i][j];
			if (map.first.valid == BLOCK) {
				total_live_pages += map.second;
				num_blocks_considered++;
			}
		}
	}
	if (num_blocks_considered == 0)
		return PAGE_SIZE;
	double avg = (double)total_live_pages / num_blocks_considered;
	return avg;
}*/
