#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

DFTL::DFTL(Ssd *ssd) :
		cached_mapping_table(),
		global_translation_directory(NUMBER_OF_ADDRESSABLE_BLOCKS()),
		ongoing_mapping_operations(),
		application_ios_waiting_for_translation(),
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		page_mapping(FtlImpl_Page(ssd)),
		CACHED_ENTRIES_THRESHOLD(1000),
		num_dirty_cached_entries(0),
		dial(0)
{}

DFTL::DFTL() :
		NUM_PAGES_IN_SSD(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE),
		CACHED_ENTRIES_THRESHOLD(1000)
{}

DFTL::~DFTL(void)
{}

void DFTL::read(Event *event)
{
	submit_or_translate(event);
}

void DFTL::register_read_completion(Event const& event, enum status result) {

	// if normal application read, do nothing
	if (ongoing_mapping_operations.count(event.get_application_io_id()) == 0) {
		return;
	}
	// If mapping read

	// identify translation page we finished reading
	long translation_page_id = ongoing_mapping_operations[event.get_application_io_id()];
	ongoing_mapping_operations.erase(event.get_application_io_id());

	// Insert all entries into cached mapping table with hotness 0
	for (int i = translation_page_id * BLOCK_SIZE; i < 512; i++) {
		if (cached_mapping_table.count(i) == 0) {
			cached_mapping_table[i] = entry();
		}
	}

	// schedule all operations
	vector<Event*> waiting_events = application_ios_waiting_for_translation[translation_page_id];
	application_ios_waiting_for_translation.erase(translation_page_id);
	for (auto e : waiting_events) {
		// TODO: set physical address for this IO
		scheduler->schedule_event(e);
	}


}

void DFTL::write(Event *event)
{
	submit_or_translate(event);
}

void DFTL::register_write_completion(Event const& event, enum status result) {
	page_mapping.register_write_completion(event, result);

	// If the write that just finished is a normal IO, update the mapping
	if (ongoing_mapping_operations.count(event.get_application_io_id()) == 0) {
		entry& e = cached_mapping_table[event.get_logical_address()];
		e.phys_addr = event.get_address();
		e.fixed = false;
		e.dirty = true;
		if (++num_dirty_cached_entries == CACHED_ENTRIES_THRESHOLD) {
			flush_mapping();
		}
		return;
	}
	// if write is a mapping IO that just finished
	long translation_page_id = ongoing_mapping_operations[event.get_application_io_id()];
	ongoing_mapping_operations.erase(event.get_application_io_id());

	// mark all pages included as clean

}

void DFTL::submit_or_translate(Event *event) {
	long la = event->get_logical_address();
	// If the logical address is in the cached mapping table, submit the IO
	if (cached_mapping_table.count(la) == 1) {
		entry& e = cached_mapping_table[la];
		event->set_address(e.phys_addr);
		e.hotness++;
		scheduler->schedule_event(event);
		return;
	}

	// find which translation page is the logical address is on
	long translation_page_id = la / BLOCK_SIZE;
	Address physical_addr_of_translation_page = global_translation_directory[translation_page_id];

	// There is an ongoing IO to get this translation page
	if (application_ios_waiting_for_translation.count(translation_page_id) == 1) {
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
	}
	else {
		Event* mapping_event = new Event(READ, NUM_PAGES_IN_SSD - translation_page_id, 1, event->get_current_time());
		mapping_event->set_mapping_op(true);
		mapping_event->set_address(physical_addr_of_translation_page);
		application_ios_waiting_for_translation[translation_page_id] = vector<Event*>();
		application_ios_waiting_for_translation[translation_page_id].push_back(event);
		ongoing_mapping_operations[mapping_event->get_application_io_id()] = translation_page_id;
		scheduler->schedule_event(mapping_event);
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

// Uses a clock entry replacement policy
void DFTL::flush_mapping() {

	// start at a given location
	// find first entry with hotness 0 and make that the target, or make full traversal and identify least hot entry
	long victim;
	short min_hotness = SHRT_MAX;

	for (auto it = cached_mapping_table.upper_bound(dial); it != cached_mapping_table.lower_bound(dial); ++it) {
		entry e = (*it).second;
		entry key = (*it).first;
		if (e.dirty && e.hotness == 0 && !e.fixed) {
			victim = key;
			min_hotness = 0;
			break;
		}
		else if (e.dirty && e.hotness < min_hotness && !e.fixed) {
			victim = key;
			min_hotness = e.hotness;
		}
	}
	dial = victim;

	// find translation page. If all entries are cached, no need to read it. Otherwise, read it.
	long translation_page_id = victim / BLOCK_SIZE;
	long first_key_in_translation_page = translation_page_id * BLOCK_SIZE;
	bool are_all_mapping_entries_cached = cached_mapping_table.count(first_key_in_translation_page) == 1 &&
			cached_mapping_table.count(first_key_in_translation_page) == 1;

	long last_addr = victim;
	for (auto it = cached_mapping_table.find(first_key_in_translation_page);
			are_all_mapping_entries_cached && it != cached_mapping_table.find(first_key_in_translation_page + 512); ++it) {
		long curr_key = (*it).first;
		if (last_addr == curr_key || last_addr + 1 == curr_key) {
			last_addr = curr_key;
		} else {
			are_all_mapping_entries_cached = false;
		}
	}

	if (are_all_mapping_entries_cached) {
		// Yes
	}
	else {
		// issue mapping IO
	}

	// find translation page and first logical address in the translation page

}


