#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;
int DFTL::CACHED_ENTRIES_THRESHOLD = 10000;
int DFTL::ENTRIES_PER_TRANSLATION_PAGE = 1024;

DFTL::DFTL(Ssd *ssd, Block_manager_parent* bm) :
		FtlParent(ssd, bm),
		cached_mapping_table(),
		global_translation_directory(NUMBER_OF_ADDRESSABLE_BLOCKS(), Address()),
		ongoing_mapping_operations(),
		application_ios_waiting_for_translation(),
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		page_mapping(FtlImpl_Page(ssd, bm)),
		stats()
{
	IS_FTL_PAGE_MAPPING = true;
}

DFTL::DFTL() :
		FtlParent(),
		cached_mapping_table(),
		global_translation_directory(),
		ongoing_mapping_operations(),
		application_ios_waiting_for_translation(),
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		page_mapping()
{
	IS_FTL_PAGE_MAPPING = true;
}

DFTL::~DFTL(void)
{
	stats.print();
	assert(application_ios_waiting_for_translation.size() == 0);
}


void DFTL::read(Event *event)
{
	long la = event->get_logical_address();
	// If the logical address is in the cached mapping table, submit the IO
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table.at(la);
		e.hotness++;
		scheduler->schedule_event(event);
		return;
	}

	// find which translation page is the logical address is on
	long translation_page_id = la / ENTRIES_PER_TRANSLATION_PAGE;

	// If the mapping entry does not exist in cache and there is no translation page in flash, cancel the read
	if (global_translation_directory[translation_page_id].valid == NONE) {
		event->set_noop(true);
		return;
	}

	// If there is no mapping IO currently targeting the translation page, create on. Otherwise, invoke current event when ongoing mapping IO finishes.
	if (ongoing_mapping_operations.count(NUM_PAGES_IN_SSD - translation_page_id) == 1) {
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
	}
	else {
		//printf("creating mapping read %d for app write %d\n", translation_page_id, event->get_logical_address());
		create_mapping_read(translation_page_id, event->get_current_time(), event);
	}
}

void DFTL::register_read_completion(Event const& event, enum status result) {

	if (event.is_mapping_op()) {
		//event.print();
	}
	if (event.get_logical_address() == 1048259) {
		int i = 0;
		i++;
	}

	// if normal application read, do nothing
	if (ongoing_mapping_operations.count(event.get_logical_address()) == 0) {
		return;
	}
	// If mapping read
	stats.num_mapping_reads++;
	ongoing_mapping_operations.erase(event.get_logical_address());

	// identify translation page we finished reading
	long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);

	// Insert all entries into cached mapping table with hotness 0
	/*for (int i = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE; i < (translation_page_id + 1) * ENTRIES_PER_TRANSLATION_PAGE; i++) {
		if (cached_mapping_table.count(i) == 0) {
			cached_mapping_table[i] = entry();
			eviction_queue_clean.push(i);
		}
	}*/

	// schedule all operations
	vector<Event*> waiting_events = application_ios_waiting_for_translation[translation_page_id];
	application_ios_waiting_for_translation.erase(translation_page_id);
	for (auto e : waiting_events) {
		if (e->is_mapping_op()) {
			if (ongoing_mapping_operations.count(e->get_logical_address()) == 1) {
				assert(false);
				delete e;
				continue;
			}
			//printf("now submitting mapping write  %d\n", translation_page_id);
			ongoing_mapping_operations.insert(e->get_logical_address());
		}
		else  {
			long fixed = 0;
			if (cached_mapping_table.count(e->get_logical_address()) == 0) {
				entry entry;
				entry.hotness++;
				fixed = 0;
				cached_mapping_table[e->get_logical_address()] = entry;
			}
			else {
				entry& entry = cached_mapping_table[e->get_logical_address()];
				entry.hotness++;
				fixed = entry.fixed;
			}
			if (e->get_event_type() == WRITE && fixed == 0) {
				entry& entry = cached_mapping_table[e->get_logical_address()];
				entry.fixed++;
			}
		}
		scheduler->schedule_event(e);
	}

	// Made sure we don't overuse RAM
	try_clear_space_in_mapping_cache(event.get_current_time());
}

void DFTL::write(Event *event)
{
	long la = event->get_logical_address();

	if (event->get_application_io_id() == 3412257) {
		event->print();
		PRINT_LEVEL = 2;
		int a = 0;
		a++;
	}

	// If the logical address is in the cached mapping table, submit the IO
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table.at(la);
		e.hotness++;

		e.fixed++;
		scheduler->schedule_event(event);
		return;
	}

	// find which translation page is the logical address is on
	long translation_page_id = la / ENTRIES_PER_TRANSLATION_PAGE;

	// does this translation page exist? If not (because the SSD is new) just create an entry in the cached mapping table
	if (global_translation_directory[translation_page_id].valid == NONE) {
		entry e;
		e.fixed = 1;
		e.hotness = 1;	// make it slightly hotter than usual, in case more addresses of the same translation page will be written.
		cached_mapping_table[la] = e;
		scheduler->schedule_event(event);
		return;
	}

	// If there is no mapping IO currently targeting the translation page, create on. Otherwise, invoke current event when ongoing mapping IO finishes.
	if (ongoing_mapping_operations.count(NUM_PAGES_IN_SSD - translation_page_id) == 1) {
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
	}
	else {
		//printf("creating mapping read %d for app write %d\n", translation_page_id, event->get_logical_address());
		create_mapping_read(translation_page_id, event->get_current_time(), event);
	}
}

void DFTL::register_write_completion(Event const& event, enum status result) {
	page_mapping.register_write_completion(event, result);

	if (event.is_mapping_op()) {
		//event.print();
	}

	if (event.get_logical_address() == 1048259) {
		int i = 0;
		i++;
	}
	if (event.get_noop()) {
		return;
	}

	// assume that the logical address of a GCed page is in the out of bound area of the page, so we can use it to update the mapping
	if (event.is_garbage_collection_op() && !event.is_original_application_io() && !event.is_mapping_op()) {
		if (cached_mapping_table.count(event.get_logical_address()) == 0) {
			entry e;
			e.timestamp = event.get_current_time();
			e.dirty = true;
			cached_mapping_table[event.get_logical_address()] = e;
			eviction_queue_dirty.push(event.get_logical_address());
		}
		else {
			entry& e = cached_mapping_table[event.get_logical_address()];
			e.dirty = true;
			e.timestamp = event.get_current_time();
			e.fixed = 0;
		}
		return;
	}

	// If the write that just finished is a normal IO, update the mapping
	if (ongoing_mapping_operations.count(event.get_logical_address()) == 0 && !event.is_mapping_op()) {
		if (cached_mapping_table.count(event.get_logical_address()) == 0) {
			printf("The entry was not in the cache \n");
			event.print();
			assert(false);
		}
		entry& e = cached_mapping_table.at(event.get_logical_address());
		e.fixed = 0;
		e.dirty = true;
		eviction_queue_dirty.push(event.get_logical_address());
		e.timestamp = event.get_current_time();
		/*if (num_dirty_cached_entries == CACHED_ENTRIES_THRESHOLD) {
			flush_mapping(event.get_current_time());
		}*/
		try_clear_space_in_mapping_cache(event.get_current_time());
		return;
	}

	// if write is a mapping IO that just finished
	stats.num_mapping_writes++;
	if (stats.num_mapping_writes % 100 == 0) {
		printf("writes %d\n, ", stats.num_mapping_writes);
	}
	ongoing_mapping_operations.erase(event.get_logical_address());
	long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);

	//printf("finished mapping write %d\n", translation_page_id);
	global_translation_directory[translation_page_id] = event.get_address();

	// mark all pages included as clean
	lock_all_entries_in_a_translation_page(translation_page_id, event.get_start_time());

	// really, we should not be exceeding the threshold here, but just in case we are, evict entries
	try_clear_space_in_mapping_cache(event.get_current_time());

	// schedule all operations
	vector<Event*> waiting_events = application_ios_waiting_for_translation[translation_page_id];
	application_ios_waiting_for_translation.erase(translation_page_id);
	for (auto e : waiting_events) {
		if (e->get_event_type() == READ) {
			read(e);
		} else {
			write(e);
		}
	}
}


void DFTL::trim(Event *event)
{
	// For now we don't handle trims for DFTL
	assert(false);
}

void DFTL::register_trim_completion(Event & event) {
	page_mapping.register_trim_completion(event);
}

long DFTL::get_logical_address(uint physical_address) const {
	return page_mapping.get_logical_address(physical_address);
}

Address DFTL::get_physical_address(uint logical_address) const {
	return page_mapping.get_physical_address(logical_address);
}

void DFTL::set_replace_address(Event& event) const {
	if (event.is_mapping_op()) {
		long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);
		Address ra = global_translation_directory[translation_page_id];
		event.set_replace_address(ra);
	}
	// We are garbage collecting a mapping IO, we can infer it by the page's logical address
	else if (event.is_garbage_collection_op() && event.get_logical_address() >= NUM_PAGES_IN_SSD - global_translation_directory.size()) {
		long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);
		Address ra = global_translation_directory[translation_page_id];
		event.set_replace_address(ra);
		event.set_mapping_op(true);
	}
	else {
		page_mapping.set_replace_address(event);
	}
}

void DFTL::set_read_address(Event& event) const {
	if (event.is_mapping_op()) {
		long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);
		Address ra = global_translation_directory[translation_page_id];
		event.set_address(ra);
	}
	// We are garbage collecting a mapping IO, we can infer it by the page's logical address
	else if (event.is_garbage_collection_op() && event.get_logical_address() >= NUM_PAGES_IN_SSD - global_translation_directory.size()) {
		long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);
		Address ra = global_translation_directory[translation_page_id];
		event.set_address(ra);
	}
	else {
		page_mapping.set_read_address(event);
	}
}

void DFTL::lock_all_entries_in_a_translation_page(long translation_page_id, double time) {
	int num_dirty_entries = 0;
	long first_key_in_translation_page = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE;
	for (int i = first_key_in_translation_page;
			i < first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE; ++i) {
		if (cached_mapping_table.count(i) == 0) {
			continue;
		}
		long curr_key = i;
		entry& e = cached_mapping_table[i];
		assert(e.fixed >= 0);
		if (e.timestamp <= time && e.dirty) {
			e.dirty = false;
			num_dirty_entries++;
		}
	}

	//printf("mapping write %d cleans: %d   num dirty: %d   cache size: %d\n", translation_page_id, num_dirty_entries, get_num_dirty_entries(), cached_mapping_table.size());
}

void DFTL::try_clear_space_in_mapping_cache(double time) {
	while (cached_mapping_table.size() >= CACHED_ENTRIES_THRESHOLD && flush_mapping(time, false));
	if (cached_mapping_table.size() >= CACHED_ENTRIES_THRESHOLD) {
		flush_mapping(time, true);
	}
}

void DFTL::create_mapping_read(long translation_page_id, double time, Event* dependant) {
	Event* mapping_event = new Event(READ, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	if (mapping_event->get_logical_address() == 1048259) {
		int i = 0;
		i++;
	}
	mapping_event->set_mapping_op(true);
	Address physical_addr_of_translation_page = global_translation_directory[translation_page_id];
	mapping_event->set_address(physical_addr_of_translation_page);
	application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
	application_ios_waiting_for_translation[translation_page_id].push_back(dependant);
	assert(ongoing_mapping_operations.count(mapping_event->get_logical_address()) == 0);
	ongoing_mapping_operations.insert(mapping_event->get_logical_address());
	scheduler->schedule_event(mapping_event);
}

int DFTL::get_num_dirty_entries() const {
	int num_dirty = 0;
	for (auto i : cached_mapping_table) {
		if (i.second.dirty) {
			num_dirty++;
		}
	}
	return num_dirty;
}

void DFTL::iterate(long& victim_key, entry& victim_entry, bool allow_choosing_dirty) {
	queue<long>& queue = allow_choosing_dirty ? eviction_queue_dirty : eviction_queue_clean;
	for (int i = 0; i < queue.size(); i++) {
		long addr = queue.front();
		queue.pop();
		entry& e = cached_mapping_table[addr];
		if (!allow_choosing_dirty && e.dirty) {
			eviction_queue_dirty.push(addr);
		}
		else if (allow_choosing_dirty && !e.dirty) {
			eviction_queue_clean.push(addr);
		}
		else if (!e.fixed && e.hotness == 0) {
			victim_key = addr;
			victim_entry = e;
			return;
		}
		else if (e.dirty == allow_choosing_dirty) {
			e.hotness = e.hotness == 0 ? 0 : e.hotness - 1;
			queue.push(addr);
		}
	}
}

// Uses a clock entry replacement policy
bool DFTL::flush_mapping(double time, bool allow_flushing_dirty) {
	//printf("flush called. num dirty: %d     cache size: %d\n", get_num_dirty_entries(), cached_mapping_table.size());
	// start at a given location
	// find first entry with hotness 0 and make that the target, or make full traversal and identify least hot entry
	long victim = UNDEFINED;
	entry victim_entry;
	victim_entry.hotness = SHRT_MAX;

	iterate(victim, victim_entry, allow_flushing_dirty);

	if (victim == UNDEFINED) {
		//printf("Warning, could not find a victim to flush from cache\n");
		return false;
	}

	// find translation page. If all entries are cached, no need to read it. Otherwise, read it.
	long translation_page_id = victim / ENTRIES_PER_TRANSLATION_PAGE;
	//printf("flush  %d\n", translation_page_id);
	if (ongoing_mapping_operations.count(NUM_PAGES_IN_SSD - translation_page_id) == 1) {
		//printf("Ongoing flush already targeting this address\n");
		return false;
	}

	// if entry is clean, just erase it. Otherwise, need some mapping IOs.
	if (!victim_entry.dirty) {
		//printf("erase %d\n", victim);
		cached_mapping_table.erase(victim);
		return true;
	}

	// create mapping write
	Event* mapping_event = new Event(WRITE, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	mapping_event->set_mapping_op(true);

	// If a translation page on flash does not exist yet, we can flush without a read first
	if( global_translation_directory[translation_page_id].valid == NONE ) {
		application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
		ongoing_mapping_operations.insert(mapping_event->get_logical_address());
		scheduler->schedule_event(mapping_event);
		//printf("submitting mapping write %d\n", translation_page_id);
		return true;
	}

	// If the translation page already exists, check if all entries belonging to it are in the cache.
	long first_key_in_translation_page = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE;
	bool are_all_mapping_entries_cached = false;

	long last_addr = first_key_in_translation_page;
	/*for (int i = first_key_in_translation_page; i < first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE; ++i) {
		if (cached_mapping_table.count(i) == 0) {
			are_all_mapping_entries_cached = false;
			break;
		}
	}*/

	if (are_all_mapping_entries_cached) {
		application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
		ongoing_mapping_operations.insert(mapping_event->get_logical_address());
		//printf("submitting mapping write, all in RAM %d\n", translation_page_id);
		scheduler->schedule_event(mapping_event);
	}
	else {
		//printf("submitting mapping read, since not all entries are in RAM %d\n", translation_page_id);
		create_mapping_read(translation_page_id, time, mapping_event);
	}
	return true;
}

void DFTL::print_short() const {
	int num_dirty = 0;
	int num_fixed = 0;
	int num_cold = 0;
	int num_hot = 0;
	int num_super_hot = 0;
	for (auto i : cached_mapping_table) {
		if (i.second.dirty) num_dirty++;
		if (i.second.fixed) num_fixed++;
		if (i.second.hotness == 0) num_cold++;
		if (i.second.hotness == 1) num_hot++;
		if (i.second.hotness > 1) num_super_hot++;
	}
	printf("total: %d\tdirty: %d\tfixed: %d\tcold: %d\thot: %d\tvery hot: %d\tnum ios: %d\n", cached_mapping_table.size(), num_dirty, num_fixed, num_cold, num_hot, num_super_hot, StatisticsGatherer::get_global_instance()->total_writes());
}

// used for debugging
void DFTL::print() const {
	int j = 0;
	for (auto i : cached_mapping_table) {
		printf("%d\t%d\t%d\t%d\t%d\t\n", j++, i.first, i.second.dirty, i.second.fixed, i.second.hotness);
	}

	for (auto i : application_ios_waiting_for_translation) {
		for (auto e : i.second) {
			printf("waiting for %i", i.first);
			e->print();
		}
	}
}

void DFTL::stats::print() const {
	printf("DFTL mapping reads\t%d\n", num_mapping_reads);
	printf("DFTL mapping writes\t%d\n", num_mapping_writes);
}
