#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

DFTL::DFTL(Ssd *ssd, Block_manager_parent* bm) :
		FtlParent(ssd, bm),
		cached_mapping_table(),
		global_translation_directory(NUMBER_OF_ADDRESSABLE_BLOCKS(), Address()),
		ongoing_mapping_operations(),
		application_ios_waiting_for_translation(),
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		page_mapping(FtlImpl_Page(ssd, bm)),
		CACHED_ENTRIES_THRESHOLD(DFTL_CACHE_SIZE),
		num_dirty_cached_entries(0),
		dial(0),
		ENTRIES_PER_TRANSLATION_PAGE(DFTL_ENTRIES_PER_TRANSLATION_PAGE)
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
		page_mapping(),
		CACHED_ENTRIES_THRESHOLD(),
		num_dirty_cached_entries(),
		dial(),
		ENTRIES_PER_TRANSLATION_PAGE(512)
{
	IS_FTL_PAGE_MAPPING = true;
}

DFTL::~DFTL(void)
{
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
	// if normal application read, do nothing
	if (ongoing_mapping_operations.count(event.get_logical_address()) == 0) {
		return;
	}
	// If mapping read
	ongoing_mapping_operations.erase(event.get_logical_address());

	// identify translation page we finished reading
	long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);

	// Insert all entries into cached mapping table with hotness 0
	for (int i = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE; i < (translation_page_id + 1) * ENTRIES_PER_TRANSLATION_PAGE; i++) {
		if (cached_mapping_table.count(i) == 0) {
			cached_mapping_table[i] = entry();
		}
	}

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
			//lock_all_entries_in_a_translation_page(translation_page_id, -1, event.get_current_time());
			ongoing_mapping_operations.insert(e->get_logical_address());
		}
		else if (e->get_event_type() == WRITE) {
			assert(cached_mapping_table.count(e->get_logical_address()) == 1);
			entry& en = cached_mapping_table.at(e->get_logical_address());
			en.hotness++;
			en.timestamp = numeric_limits<double>::infinity();
			if (en.fixed == 0) {
				en.fixed++;
			}
		}
		else {
			assert(cached_mapping_table.count(e->get_logical_address()) == 1);
			entry& en = cached_mapping_table.at(e->get_logical_address());
			en.hotness++;
		}
		scheduler->schedule_event(e);
	}

	// Made sure we don't overuse RAM
	int num_evicted = 1;
	while (cached_mapping_table.size() >= CACHED_ENTRIES_THRESHOLD && num_evicted > 0) {
		num_evicted = evict_cold_entries();
	}

	if (cached_mapping_table.size() >= CACHED_ENTRIES_THRESHOLD) {
		flush_mapping(event.get_current_time());
	}

}

void DFTL::write(Event *event)
{
	long la = event->get_logical_address();

	if (event->get_id() == 101850) {
		int i =0 ;
		i++;
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
		e.hotness = 1;
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
		}
		else {
			entry& e = cached_mapping_table[event.get_logical_address()];
			e.dirty = true;
			e.timestamp = event.get_current_time();
			e.fixed = 0;
		}
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
		e.timestamp = event.get_current_time();
		if (++num_dirty_cached_entries == CACHED_ENTRIES_THRESHOLD) {
			//printf("num_dirty_cached_entries  %d\n", num_dirty_cached_entries);
			flush_mapping(event.get_current_time());
		}
		return;
	}

	// if write is a mapping IO that just finished
	ongoing_mapping_operations.erase(event.get_logical_address());
	long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);

	//printf("finished mapping write %d\n", translation_page_id);
	global_translation_directory[translation_page_id] = event.get_address();

	// mark all pages included as clean
	lock_all_entries_in_a_translation_page(translation_page_id, -1, event.get_start_time());

	// really, we should not be exceeding the threshold here, but just in case we are, evict entries
	if (CACHED_ENTRIES_THRESHOLD <= cached_mapping_table.size()) {
		//printf("Warning: more entries in DFTL cache than capacity allows\n");
		int num_flushed = evict_cold_entries();
		//printf("num flushed %d\n", num_flushed);
	}

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

void DFTL::lock_all_entries_in_a_translation_page(long translation_page_id, int lock, double time) {
	long first_key_in_translation_page = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE;
	for (auto it = cached_mapping_table.lower_bound(first_key_in_translation_page);
			it != cached_mapping_table.upper_bound(first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE); ++it) {
		long curr_key = (*it).first;
		entry& e = (*it).second;

		if (lock == 1 && e.timestamp <= time && e.fixed == 0 && e.dirty) {
			e.fixed++;
		}

		assert(e.fixed >= 0);

		if (lock == -1 && e.fixed > 0 && e.timestamp <= time && e.dirty) {
			e.dirty = false;
			e.fixed--;
			num_dirty_cached_entries--;
		}
	}
}

// Iterates through all entries in the cache, identifying and evicting X entries that are not dirty with the minimum temperature
// returns the number of entries evicted. 0 is returned if all entries are dirty and nothing could be evicted.
int DFTL::evict_cold_entries() {
	vector<long> keys_to_remove;
	vector<entry> entries;
	int min_temperature = INT_MAX;
	for (auto e : cached_mapping_table) {
		if (!e.second.dirty && e.second.fixed == 0 && e.second.hotness <= min_temperature) {
			min_temperature = e.second.hotness;
			keys_to_remove.push_back(e.first);
			entries.push_back(e.second);
		}
	}
	int num_removed = 0;
	int how_many_to_remove = cached_mapping_table.size() - CACHED_ENTRIES_THRESHOLD;
	for (int i = 0; i < entries.size(); i++) {
		if (entries[i].hotness == min_temperature) {
			assert(entries[i].fixed == 0);
			if (keys_to_remove[i] == 44584) {
				int i = 0;
				i++;
			}
			cached_mapping_table.erase(keys_to_remove[i]);
			if (++num_removed == how_many_to_remove) {
				return num_removed;
			}
		}
	}
	return num_removed;
}

void DFTL::create_mapping_read(long translation_page_id, double time, Event* dependant) {
	Event* mapping_event = new Event(READ, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	mapping_event->set_mapping_op(true);
	Address physical_addr_of_translation_page = global_translation_directory[translation_page_id];
	mapping_event->set_address(physical_addr_of_translation_page);
	application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
	application_ios_waiting_for_translation[translation_page_id].push_back(dependant);
	assert(ongoing_mapping_operations.count(mapping_event->get_logical_address()) == 0);
	ongoing_mapping_operations.insert(mapping_event->get_logical_address());
	scheduler->schedule_event(mapping_event);
}

void DFTL::iterate(long& victim_key, entry& victim_entry, map<long, entry>::iterator start, map<long, entry>::iterator finish) {
	for (auto it = start; it != finish; ++it) {
		entry e = (*it).second;
		long key = (*it).first;
		if (e.dirty && e.hotness == 0 && e.fixed == 0) {
			victim_key = key;
			victim_entry = e;
			break;
		}
		else if (e.dirty && e.hotness < victim_entry.hotness && e.fixed == 0) {
			victim_key = key;
			victim_entry = e;
		}
		e.hotness--;
	}
}

// Uses a clock entry replacement policy
void DFTL::flush_mapping(double time) {

	// start at a given location
	// find first entry with hotness 0 and make that the target, or make full traversal and identify least hot entry
	long victim = UNDEFINED;
	entry victim_entry;
	victim_entry.hotness = SHRT_MAX;

	map<long, entry>::iterator start = cached_mapping_table.upper_bound(dial);
	map<long, entry>::iterator finish = cached_mapping_table.end();
	iterate(victim, victim_entry, start, finish);
	if (victim_entry.hotness > 0) {
		map<long, entry>::iterator start = cached_mapping_table.begin();
		map<long, entry>::iterator finish = cached_mapping_table.lower_bound(dial);
		iterate(victim, victim_entry, start, finish);
	}

	if (victim == UNDEFINED) {
		//printf("Warning, could not find a victim to flush from cache\n");
		return;
	}

	if (dial > victim + ENTRIES_PER_TRANSLATION_PAGE || dial > NUM_PAGES_IN_SSD * OVER_PROVISIONING_FACTOR) {
		dial = 0;
	} else {
		dial += ENTRIES_PER_TRANSLATION_PAGE;
	}

	// find translation page. If all entries are cached, no need to read it. Otherwise, read it.
	long translation_page_id = victim / ENTRIES_PER_TRANSLATION_PAGE;
	//printf("flush  %d\n", translation_page_id);
	if (ongoing_mapping_operations.count(NUM_PAGES_IN_SSD - translation_page_id) == 1) {
		//printf("Ongoing flush already targeting this address\n");
		return;
	}

	// create mapping write
	Event* mapping_event = new Event(WRITE, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	mapping_event->set_mapping_op(true);
	lock_all_entries_in_a_translation_page(translation_page_id, 1, time);

	// If a translation page on flash does not exist yet, we can flush without a read first
	if( global_translation_directory[translation_page_id].valid == NONE ) {
		application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
		ongoing_mapping_operations.insert(mapping_event->get_logical_address());
		scheduler->schedule_event(mapping_event);
		//printf("submitting mapping write %d\n", translation_page_id);
		return;
	}

	// If the translation page already exists, check if all entries belonging to it are in the cache.
	long first_key_in_translation_page = translation_page_id * ENTRIES_PER_TRANSLATION_PAGE;
	bool are_all_mapping_entries_cached = cached_mapping_table.count(first_key_in_translation_page) == 1 &&
			cached_mapping_table.count(first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE) == 1;

	long last_addr = first_key_in_translation_page;
	for (auto it = cached_mapping_table.find(first_key_in_translation_page);
			are_all_mapping_entries_cached && it != cached_mapping_table.find(first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE); ++it) {
		long curr_key = (*it).first;
		if (last_addr == curr_key || last_addr + 1 == curr_key) {
			last_addr = curr_key;
		} else {
			are_all_mapping_entries_cached = false;
		}
	}

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
