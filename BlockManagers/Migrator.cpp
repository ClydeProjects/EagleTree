#include "../ssd.h"
using namespace ssd;

Migrator::Migrator() :
		scheduler(NULL), bm(NULL), gc(NULL), wl(NULL), ftl(NULL), ssd(NULL), page_copy_back_count(),
		num_blocks_being_garbaged_collected_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
		blocks_being_garbage_collected(),
		num_erases_scheduled_per_package(SSD_SIZE),
		dependent_gc(),
		gc_time_stat()
{
}

Migrator::~Migrator() {
	/*for (int i = 0; i < num_blocks_being_garbaged_collected_per_LUN.size(); i++) {
		for (int j = 0; j < num_blocks_being_garbaged_collected_per_LUN[i].size(); j++) {
			int num = num_blocks_being_garbaged_collected_per_LUN[i][j];
			printf("num_blocks_being_garbaged_collected_per_LUN  %d  %d:  %d\n", i, j, num);
		}
	}
	map<int, int>::iterator it = blocks_being_garbage_collected.begin();
	while (it != blocks_being_garbage_collected.end()) {
		printf("blocks_being_garbage_collected    %d:  %d\n", (*it).first, (*it).second);
		it++;
	}*/
	printf("average time for a whole GC operation:\t%f\n", StatisticData::get_average("gc_op_length", 0));
	delete gc;
	delete wl;
}

void Migrator::init(IOScheduler* new_s, Block_manager_parent* new_bm, Garbage_Collector* new_gc, Wear_Leveling_Strategy* new_wl, FtlParent* new_ftl, Ssd* new_ssd) {
	scheduler = new_s;
	bm = new_bm;
	gc = new_gc;
	wl = new_wl;
	ftl = new_ftl;
	ssd = new_ssd;
}

void Migrator::register_event_completion(Event* event) {
	if (event->get_event_type() == ERASE) {
		handle_erase_completion(event);
	}
	else if (event->get_event_type() == TRIM || (event->get_event_type() == WRITE && event->get_replace_address().valid != NONE)) {
		handle_trim_completion(event);
	}
	gc->register_event_completion(*event);
}

void Migrator::handle_erase_completion(Event* event) {
	Address const& a = event->get_address();

	if (USE_ERASE_QUEUE) {
		assert(num_erases_scheduled_per_package[a.package] == 1);
		num_erases_scheduled_per_package[a.package]--;
		assert(num_erases_scheduled_per_package[a.package] == 0);

		if (erase_queue[a.package].size() > 0) {
			Event* new_erase = erase_queue[a.package].front();
			double diff = event->get_current_time() - new_erase->get_current_time();
			new_erase->incr_bus_wait_time(diff);
			erase_queue[a.package].pop();
			num_erases_scheduled_per_package[a.package]++;
			scheduler->schedule_event(new_erase);
		}
	}

	Block* block = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	double time_to_completion = 0;
	if (gc_time_stat.count(block) == 1) {
		double time_to_completion = event->get_current_time() - gc_time_stat.at(block);
	}
	gc_time_stat.erase(block);
	StatisticData::register_statistic("gc_op_length", {
			new Integer(time_to_completion)
	});

	num_blocks_being_garbaged_collected_per_LUN[a.package][a.die]--;
	blocks_being_garbage_collected.erase(a.get_linear_address());

	if (PRINT_LEVEL > 1) {
		printf("Finishing GC in %d \n", a.get_linear_address());
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (auto i : blocks_being_garbage_collected) {
			printf("%d  ", i.first);
		}
		printf("\n");
	}
}

void Migrator::handle_trim_completion(Event* event) {
	Address ra = event->get_replace_address();
	Block& block = *ssd->get_package(ra.package)->get_die(ra.die)->get_plane(ra.plane)->get_block(ra.block);
	Page const& page = block.get_page(ra.page);
	uint age_class = bm->sort_into_age_class(ra);
	long const phys_addr = block.get_physical_address();

	assert(page.get_state() == VALID);
	block.invalidate_page(ra.page);
	assert(block.get_state() != FREE);

	/*if (event->get_replace_address().package == 3 && event->get_replace_address().die == 0 && event->get_replace_address().block == 881) {
		printf("num pages live: %d\n", block.get_pages_valid());
	}*/

	ra.valid = BLOCK;
	ra.page = 0;
	assert(ra.get_linear_address() == phys_addr);

	if (blocks_being_garbage_collected.count(block.get_physical_address()) == 1) {
		assert(blocks_being_garbage_collected.at(block.get_physical_address()) > 0);
		blocks_being_garbage_collected.at(block.get_physical_address())--;
	}

	/*if (blocks_being_garbage_collected.count(phys_addr) == 0 && block.get_state() == INACTIVE) {
		gc->commit_choice_of_victim(ra);
		blocks_being_garbage_collected[phys_addr] = 0;
		num_blocks_being_garbaged_collected_per_LUN[ra.package][ra.die]++;
		issue_erase(ra, event->get_current_time());
	}
	else*/ if (blocks_being_garbage_collected.count(phys_addr) == 1 && blocks_being_garbage_collected.at(phys_addr) == 0) {
		assert(block.get_state() == INACTIVE);
		blocks_being_garbage_collected[phys_addr]--;
		issue_erase(ra, event->get_current_time());
	}
}

uint Migrator::how_many_gc_operations_are_scheduled() const {
	return blocks_being_garbage_collected.size();
}

void Migrator::issue_erase(Address ra, double time) {
	ra.valid = BLOCK;
	ra.page = 0;



	Event* erase = new Event(ERASE, 0, 1, time);
	erase->set_address(ra);
	erase->set_garbage_collection_op(true);

	if (PRINT_LEVEL > 1) {
		printf("block %lu", ra.get_linear_address()); printf(" is now invalid. An erase is issued: "); erase->print();
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (auto i : blocks_being_garbage_collected) {
			printf("%d  ", i.first);
		}
		printf("\n");
	}

	//int num_free_blocks = get_num_free_blocks(ra.package, ra.die);
	//assert(num_erases_scheduled_per_package[ra.package] <= 1 && num_erases_scheduled_per_package[ra.package] >= 0);
	bool there_is_already_at_least_one_erase_scheduled_on_this_channel = num_erases_scheduled_per_package[ra.package] > 0;

	if (USE_ERASE_QUEUE && there_is_already_at_least_one_erase_scheduled_on_this_channel /* && num_free_blocks > 0 &&  has_free_pages(free_block_pointers[ra.package][ra.die]) */) {
		erase_queue[ra.package].push(erase);
	}
	else {
		num_erases_scheduled_per_package[ra.package]++;
		scheduler->schedule_event(erase);
	}
}

// schedules a garbage collection operation to occur at a given time, and optionally for a given channel, LUN or age class
// the block to be reclaimed is chosen when the gc operation is initialised
void Migrator::schedule_gc(double time, int package, int die, int block, int klass) {
	Event *gc_event = new Event(GARBAGE_COLLECTION, 0, BLOCK_SIZE, time);
	Address address;
	address.package = package;
	address.die = die;
	address.block = block;

	if (package == UNDEFINED && die == UNDEFINED && block == UNDEFINED) {
		address.valid = NONE;
	} else if (package >= 0 && die == UNDEFINED && block == UNDEFINED) {
		address.valid = PACKAGE;
	} else if (package >= 0 && die >= 0 && block == UNDEFINED) {
		address.valid = DIE;
	} else if (package >= 0 && die >= 0 && block >= 0) {
		address.valid = BLOCK;
		// TODO add the wear leveling as a parameter to this method
		//gc_event->set_wear_leveling_op(true);
	} else {
		assert(false);
	}

	gc_event->set_noop(true);
	gc_event->set_address(address);
	gc_event->set_age_class(klass);
	gc_event->set_garbage_collection_op(true);

	if (PRINT_LEVEL > 1) {
		//StateTracer::print();
		printf("scheduling gc in (%d %d %d %d)  -  ", package, die, block, klass); gc_event->print();
	}
	scheduler->schedule_event(gc_event);
}

// Returns true if a copy back is allowed on a given logical address
bool Migrator::copy_back_allowed_on(long logical_address) {
	if (MAX_REPEATED_COPY_BACKS_ALLOWED <= 0 || MAX_ITEMS_IN_COPY_BACK_MAP <= 0) return false;
	//map<long, uint>::iterator copy_back_count = page_copy_back_count.find(logical_address);
	bool address_in_map = page_copy_back_count.count(logical_address) == 1; //(copy_back_count != page_copy_back_count.end());
	// If address is not in map and map is full, or if page has already been copy backed as many times as allowed, copy back is not allowed
	if ((!address_in_map && page_copy_back_count.size() >= MAX_ITEMS_IN_COPY_BACK_MAP) ||
		( address_in_map && page_copy_back_count[logical_address] >= MAX_REPEATED_COPY_BACKS_ALLOWED)) return false;
	else return true;
}

// Updates map keeping track of performed copy backs for each logical address
void Migrator::register_copy_back_operation_on(uint logical_address) {
	page_copy_back_count[logical_address]++; // Increment copy back counter for target page (if address is not yet in map, it will be inserted and count will become 1)
}

// Signals than an ECC check has been performed on a page, meaning that it can be copy backed again in the future
void Migrator::register_ECC_check_on(uint logical_address) {
	page_copy_back_count.erase(logical_address);
}

void Migrator::update_structures(Address const& a, double time) {
	Block* victim = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	gc->commit_choice_of_victim(a, time);
	blocks_being_garbage_collected[victim->get_physical_address()] = victim->get_pages_valid();
	num_blocks_being_garbaged_collected_per_LUN[a.package][a.die]++;
	StatisticsGatherer::get_global_instance()->register_executed_gc(*victim);
}

vector<deque<Event*> > Migrator::migrate(Event* gc_event) {
	Address a = gc_event->get_address();
	vector<deque<Event*> > migrations;
	if (how_many_gc_operations_are_scheduled() >= MAX_CONCURRENT_GC_OPS) {
		return migrations;
	}
	/*bool scheduled_erase_successfully = schedule_queued_erase(a);
	if (scheduled_erase_successfully) {
		return migrations;
	}*/

	int die_id = a.valid >= DIE ? a.die : UNDEFINED;
	int package_id = a.valid >= PACKAGE ? a.package : UNDEFINED;

	bool is_wear_leveling_op = gc_event->is_wear_leveling_op();

	if (gc_event->get_id() == 2332741) {
		int i = 0;
		i++;
	}

	Block * victim;
	if (a.valid == BLOCK) {
		victim = ssd->get_package(a.package)->get_die(a.die)->get_plane(a.plane)->get_block(a.block);
	}
	else {
		victim = gc->choose_gc_victim(package_id, die_id, gc_event->get_age_class());
	}

	StatisticsGatherer::get_global_instance()->register_scheduled_gc(*gc_event);

	if (victim == NULL) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_no_candidate++;
		return migrations;
	}

	if (bm->get_num_pages_available_for_new_writes() < victim->get_pages_valid()) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_not_enough_free_space++;
		return migrations;
	}

	Address addr = Address(victim->get_physical_address(), BLOCK);

	if (num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die] >= 1) {
		StatisticsGatherer::get_global_instance()->num_gc_cancelled_gc_already_happening++;
		return migrations;
	}

	/*if (blocks_being_wl.count(victim) == 1) {
		return migrations;
	}*/

	/*if (is_wear_leveling_op && !wl->schedule_wear_leveling_op(victim)) {
		return migrations;
	}*/

	if (victim->get_physical_address() == 976 && gc_event->get_start_time() > 39548840) {
		int i = 0;
		i++;
	}

	if (victim->get_state() == FREE) {
		//printf("warning: trying to garbage collect a block that is completely free. This will be ignored.\n");
		return migrations;
	}

	if (victim->get_state() == PARTIALLY_FREE) {
		//printf("warning: trying to garbage collect a block that is partially free. This will be ignored.\n");
		return migrations;
	}

	if (!bm->may_garbage_collect_this_block(victim, gc_event->get_current_time())) {
		return migrations;
	}

	update_structures(addr, gc_event->get_current_time());
	//printf("blocks being gced %d\n", blocks_being_garbage_collected.size());
	bm->subtract_from_available_for_new_writes(victim->get_pages_valid());

	if (PRINT_LEVEL > 1) {
		printf("num gc operations in (%d %d) : %d  ", addr.package, addr.die, num_blocks_being_garbaged_collected_per_LUN[addr.package][addr.die]);
		printf("Triggering GC in %ld    time: %f  ", victim->get_physical_address(), gc_event->get_current_time()); addr.print(); printf(". Migrating %d \n", victim->get_pages_valid());
		printf("%lu GC operations taking place now. On:   ", blocks_being_garbage_collected.size());
		for (auto i : blocks_being_garbage_collected) {
			printf("%d  ", i.first);
		}
		printf("\n");
	}

	assert(victim->get_state() != FREE);
	assert(victim->get_state() != PARTIALLY_FREE);

	StatisticData::register_statistic("GC_eff_with_writes", {
			new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
			new Integer(victim->get_pages_valid())
	});

	StatisticData::register_field_names("GC_eff_with_writes", {
			"num_writes",
			"num_pages_to_migrate"
	});

	gc_time_stat[victim] = gc_event->get_current_time();

	if (victim->get_pages_invalid() == BLOCK_SIZE) {
		issue_erase(addr, gc_event->get_current_time());
		return migrations;
	}

	// TODO: for DFTL, we in fact do not know the LBA when we dispatch the write. We get this from the OOB. Need to fix this.
	//PRINT_LEVEL = 1;
	for (uint i = 0; i < BLOCK_SIZE; i++) {
		if (victim->get_page(i).get_state() == VALID) {
			Address addr = Address(victim->get_physical_address(), PAGE);
			addr.page = i;
			long logical_address = ftl->get_logical_address(addr.get_linear_address());
			deque<Event*> migration;

			// If a copy back is allowed, and a target page could be reserved, do it. Otherwise, just do a traditional and more expensive READ - WRITE garbage collection
			if (copy_back_allowed_on(logical_address)) {

				Event* read_command = new Event(READ_COMMAND, logical_address, 1, gc_event->get_start_time());
				read_command->set_address(addr);
				read_command->set_garbage_collection_op(true);
				read_command->set_copyback(true);

				Event* copy_back = new Event(COPY_BACK, logical_address, 1, gc_event->get_start_time());
				copy_back->set_replace_address(addr);
				copy_back->set_garbage_collection_op(true);
				copy_back->set_copyback(true);

				migration.push_back(read_command);
				migration.push_back(copy_back);
				register_copy_back_operation_on(logical_address);
				//printf("COPY_BACK MAP (Size: %d):\n", page_copy_back_count.size()); for (map<long, uint>::iterator it = page_copy_back_count.begin(); it != page_copy_back_count.end(); it++) printf(" lba %d\t: %d\n", it->first, it->second);
			} else {
				Event* read = new Event(READ, logical_address, 1, gc_event->get_current_time());
				read->set_address(addr);
				read->set_garbage_collection_op(true);

				Event* write = new Event(WRITE, logical_address, 1, gc_event->get_current_time());
				write->set_garbage_collection_op(true);
				write->set_replace_address(addr);

				if (is_wear_leveling_op) {
					read->set_wear_leveling_op(true);
					write->set_wear_leveling_op(true);
				}

				migration.push_back(read);
				migration.push_back(write);
				//register_ECC_check_on(logical_address); // An ECC check happens in a normal read-write GC operation
			}

			long block_id = addr.get_block_id();
			if (dependent_gc.count(block_id) == 0) {
				migrations.push_back(migration);
				dependent_gc[block_id] = vector<deque<Event* > >();
			}
			else {
				dependent_gc.at(block_id).push_back(migration);
			}
		}
	}
	return migrations;
}

void Migrator::print_pending_migrations() {
	for (auto block : dependent_gc) {
		cout << block.first * BLOCK_SIZE << endl;
		for (auto migration : block.second) {
			migration[0]->get_address().print();
			cout << endl;
		}
	}
}

bool Migrator::more_migrations(Event * gc_read) {
	int block_id = gc_read->get_address().get_block_id();
	if (dependent_gc.count(block_id) == 1 && dependent_gc.at(block_id).size() > 0) {
		return true;
	}
	else if (dependent_gc.count(block_id) == 1 && dependent_gc.at(block_id).size() == 0) {
		dependent_gc.erase(block_id);
	}
	return false;
}

deque<Event*> Migrator::trigger_next_migration(Event * gc_read) {
	int block_id = gc_read->get_address().get_block_id();
	assert(dependent_gc.count(block_id) == 1 && dependent_gc.at(block_id).size() > 0);
	deque<Event*> next_migration = dependent_gc.at(block_id).back();
	dependent_gc.at(block_id).pop_back();

	if ( dependent_gc.at(block_id).empty()) {
		dependent_gc.erase(block_id);
	}

	return next_migration;
}




/*bool Block_manager_parent::schedule_queued_erase(Address location) {
	int package = location.package;
	int die = location.die;
	if (location.valid == DIE && erase_queue[package][die].size() > 0) {
		Event* erase = erase_queue[package][die].front();
		erase_queue[package][die].pop();
		num_erases_scheduled[package][die]++;
		IOScheduler::instance()->schedule_event(erase);
		return true;
	}
	else if (location.valid == PACKAGE) {
		location.valid = DIE;
		for (uint i = 0; i < PACKAGE_SIZE; i++) {
			location.die = i;
			bool scheduled_an_erase = schedule_queued_erase(location);
			if (scheduled_an_erase) {
				return true;
			}
		}
		return false;
	}
	else if (location.valid == NONE) {
		location.valid = PACKAGE;
		for (uint i = 0; i < SSD_SIZE; i++) {
			location.package = i;
			bool scheduled_an_erase = schedule_queued_erase(location);
			if (scheduled_an_erase) {
				return true;
			}
		}
	}
	return false;
}*/

