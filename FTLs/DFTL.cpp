#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

DFTL::DFTL(Ssd *ssd) :
		cached_mapping_table(),
		global_translation_directory(NUMBER_OF_ADDRESSABLE_BLOCKS(), Address()),
		ongoing_mapping_operations(),
		application_ios_waiting_for_translation(),
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		page_mapping(FtlImpl_Page(ssd)),
		CACHED_ENTRIES_THRESHOLD(1000),
		num_dirty_cached_entries(0),
		dial(0),
		ENTRIES_PER_TRANSLATION_PAGE(512)
{}

DFTL::DFTL() :
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		CACHED_ENTRIES_THRESHOLD(1000),
		ENTRIES_PER_TRANSLATION_PAGE(512)
{}

DFTL::~DFTL(void)
{}

void DFTL::read(Event *event)
{
	submit_or_translate(event);
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
	for (int i = translation_page_id * BLOCK_SIZE; i < ENTRIES_PER_TRANSLATION_PAGE; i++) {
		if (cached_mapping_table.count(i) == 0) {
			cached_mapping_table[i] = entry();
		}
	}

	// schedule all operations
	vector<Event*> waiting_events = application_ios_waiting_for_translation[translation_page_id];
	application_ios_waiting_for_translation.erase(translation_page_id);
	for (auto e : waiting_events) {
		if (e->is_mapping_op()) {
			lock_all_entires_in_a_translation_page(translation_page_id, true);
		}
		// TODO: set physical address for this IO
		scheduler->schedule_event(e);
	}


}

void DFTL::write(Event *event)
{
	long la = event->get_logical_address();
	// If the logical address is in the cached mapping table, submit the IO
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table[la];
		e.hotness++;
		scheduler->schedule_event(event);
		return;
	}

	// find which translation page is the logical address is on
	long translation_page_id = la / BLOCK_SIZE;

	// does this translation page exist? If not (because the SSD is new) just create an entry in the cached mapping table
	if (global_translation_directory[translation_page_id].valid == NONE) {
		cached_mapping_table[la] = entry();
		cached_mapping_table[la].hotness++;
		scheduler->schedule_event(event);
		return;
	}

	// If there is no mapping IO currently targeting the translation page, create on. Otherwise, invoke current event when ongoing mapping IO finishes.
	if (application_ios_waiting_for_translation.count(translation_page_id) == 1) {
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
	}
	else {
		create_mapping_read(translation_page_id, event->get_current_time(), event);
	}
}

void DFTL::register_write_completion(Event const& event, enum status result) {
	page_mapping.register_write_completion(event, result);

	// TODO: This is going to fail if a GC makes the write redundant, and returns instead. The application io id will be different.
	// If the write that just finished is a normal IO, update the mapping
	if (ongoing_mapping_operations.count(event.get_logical_address()) == 0) {
		entry& e = cached_mapping_table[event.get_logical_address()];
		e.fixed = false;
		e.dirty = true;
		if (++num_dirty_cached_entries == CACHED_ENTRIES_THRESHOLD) {
			flush_mapping(event.get_current_time());
		}
		return;
	}
	ongoing_mapping_operations.erase(event.get_application_io_id());

	// if write is a mapping IO that just finished
	long translation_page_id = - (event.get_logical_address() - NUM_PAGES_IN_SSD);
	global_translation_directory[translation_page_id] = event.get_address();

	// mark all pages included as clean

}

void DFTL::submit_or_translate(Event *event) {
	long la = event->get_logical_address();
	// If the logical address is in the cached mapping table, submit the IO
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table[la];
		e.hotness++;
		scheduler->schedule_event(event);
		return;
	}

	// find which translation page is the logical address is on
	long translation_page_id = la / BLOCK_SIZE;

	// does this translation page exist? If not (because the SSD is new) just create an entry in the cached mapping table
	if (global_translation_directory[translation_page_id].valid == NONE) {
		cached_mapping_table[la] = entry();
		cached_mapping_table[la].hotness++;
		return;
	}

	// If there is no mapping IO currently targeting the translation page, create on. Otherwise, invoke current event when ongoing mapping IO finishes.
	if (application_ios_waiting_for_translation.count(translation_page_id) == 1) {
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
	}
	else {
		create_mapping_read(translation_page_id, event->get_current_time(), event);
	}
}

void DFTL::trim(Event *event)
{

}

void DFTL::register_trim_completion(Event & event) {
	page_mapping.register_trim_completion(event);
}

long DFTL::get_logical_address(uint physical_address) const {
	return 0;
}

Address DFTL::get_physical_address(uint logical_address) const {
	return Address();
}

void DFTL::set_replace_address(Event& event) const {

}

void DFTL::set_read_address(Event& event) const {

}

void DFTL::lock_all_entires_in_a_translation_page(long translation_page_id, bool lock) {
	long first_key_in_translation_page = translation_page_id * BLOCK_SIZE;
	for (auto it = cached_mapping_table.find(first_key_in_translation_page);
			it != cached_mapping_table.find(first_key_in_translation_page + ENTRIES_PER_TRANSLATION_PAGE); ++it) {
		long curr_key = (*it).first;
		entry& e = (*it).second;
		e.fixed = lock;
	}
}

void DFTL::create_mapping_read(long translation_page_id, double time, Event* dependant) {
	Event* mapping_event = new Event(READ, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	mapping_event->set_mapping_op(true);
	Address physical_addr_of_translation_page = global_translation_directory[translation_page_id];
	mapping_event->set_address(physical_addr_of_translation_page);
	application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
	application_ios_waiting_for_translation[translation_page_id].push_back(dependant);
	ongoing_mapping_operations.insert(mapping_event->get_logical_address());
	scheduler->schedule_event(mapping_event);
}

void DFTL::iterate(long& victim_key, entry& victim_entry, map<long, entry>::iterator start, map<long, entry>::iterator finish) {
	for (auto it = start; it != finish; ++it) {
		entry e = (*it).second;
		long key = (*it).first;
		if (e.dirty && e.hotness == 0 && !e.fixed) {
			victim_key = key;
			victim_entry = e;
			break;
		}
		else if (e.dirty && e.hotness < victim_entry.hotness && !e.fixed) {
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
	long victim;
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
	if (dial + ENTRIES_PER_TRANSLATION_PAGE < victim) {
		dial = victim;
	} else {
		dial += ENTRIES_PER_TRANSLATION_PAGE;
	}



	// find translation page. If all entries are cached, no need to read it. Otherwise, read it.
	long translation_page_id = victim / BLOCK_SIZE;
	long first_key_in_translation_page = translation_page_id * BLOCK_SIZE;
	bool are_all_mapping_entries_cached = cached_mapping_table.count(first_key_in_translation_page) == 1 &&
			cached_mapping_table.count(first_key_in_translation_page) == 1;

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

	// create mapping write
	Event* mapping_event = new Event(WRITE, NUM_PAGES_IN_SSD - translation_page_id, 1, time);
	mapping_event->set_mapping_op(true);
	lock_all_entires_in_a_translation_page(translation_page_id, true);

	if (are_all_mapping_entries_cached) {
		application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
		ongoing_mapping_operations.insert(mapping_event->get_logical_address());
		scheduler->schedule_event(mapping_event);
	}
	else {
		create_mapping_read(translation_page_id, time, mapping_event);
	}

	// find translation page and first logical address in the translation page

}


