#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <ctgmath>
#include "../ssd.h"

using namespace ssd;

int LSM_FTL::buffer_threshold = 128;
int LSM_FTL::SIZE_RATIO = 2;
int LSM_FTL::mapping_run::id_generator = 0;

LSM_FTL::LSM_FTL(Ssd *ssd, Block_manager_parent* bm) :
		FtlParent(ssd, bm),
		page_mapping(FtlImpl_Page(ssd, bm)),
		buffer(),
		flush_in_progress(false)

{
	IS_FTL_PAGE_MAPPING = true;
}

LSM_FTL::LSM_FTL() :
		FtlParent(),
		flush_in_progress(false)
{
	IS_FTL_PAGE_MAPPING = true;
}

LSM_FTL::~LSM_FTL(void)
{
}

void LSM_FTL::read(Event *event)
{
	if (event->is_original_application_io()) {
		if (buffer.addresses.count(event->get_logical_address()) == 1) {
			scheduler->schedule_event(event);
		}
		else {
			//print_detailed();
			ongoing_read* r = new ongoing_read();
			r->original_read = event;
			ongoing_reads.insert(r);
			attend_ongoing_read(r, event->get_current_time());
		}
	}

}

void LSM_FTL::attend_ongoing_read(ongoing_read* r, double time) {
	for (int i = mapping_tree.runs.size() - 1; i >= 0; i--) {
		mapping_run* run = mapping_tree.runs[i];
		int la = r->original_read->get_logical_address();
		if (!run->being_created && !run->obsolete && r->run_ids_attempted.count(run->id) == 0 && (/*run->contains(la) ||*/ mapping_tree.runs[i]->filter.contains(la))) {
			// in which page is it?
			r->run_ids_attempted.insert(run->id);
			int mapping_address_to_read = UNDEFINED;
			for (int i = 0; i < run->mapping_pages.size(); i++) {
				mapping_page* page = run->mapping_pages[i];
				int first = page->first_key;
				int last = page->last_key;
				if (first <= la && last >= la) {
					mapping_address_to_read = i;
					break;
				}
			}
			if (mapping_address_to_read != UNDEFINED) {
				Event* read = new Event(READ, run->starting_logical_address + mapping_address_to_read, 1, time);
				read->set_mapping_op(true);
				r->read_ios_submitted.insert(read->get_application_io_id());
				run->executing_ios.insert(read->get_application_io_id());
				scheduler->schedule_event(read);
				return;
			}
		}
	}
}

void LSM_FTL::register_read_completion(Event const& event, enum status result) {
	collect_stats(event);
	if (event.is_mapping_op()) {
		for (auto& m : merges) {
			m->check_read(event, scheduler);
		}
		ongoing_read* ongoing = NULL;
		for (auto& r : ongoing_reads) {
			if (r->read_ios_submitted.count(event.get_application_io_id())) {
				ongoing = r;
				break;
			}
		}
		if (ongoing == NULL) {
			return;
		}

		int mapping_la = event.get_logical_address();
		mapping_run* run = NULL;
		for (auto& r : mapping_tree.runs) {
			if (r->starting_logical_address <= mapping_la && r->ending_logical_address >= mapping_la) {
				run = r;
			}
		}
		if (run->executing_ios.count(event.get_application_io_id()) == 0) {
			event.print();
			ongoing->original_read->print();
		}
		assert(run->executing_ios.count(event.get_application_io_id()) == 1);
		run->executing_ios.erase(event.get_application_io_id());
		ongoing->read_ios_submitted.erase(event.get_application_io_id());
		assert(run->executing_ios.count(event.get_application_io_id()) == 0);

		assert(run != NULL);
		bool found_address = false;
		int orig_la = ongoing->original_read->get_logical_address();
		for (auto& page : run->mapping_pages) {
			if (page->first_key <= orig_la && page->last_key >= orig_la && page->addresses.count(orig_la) == 1) {
				found_address = true;
				break;
			}
		}

		erase_run(run);
		if (!found_address) {
			attend_ongoing_read(ongoing, event.get_current_time());
		}
		else {
			Event* original_read = ongoing->original_read;
			Address addr = page_mapping.get_physical_address(original_read->get_logical_address());
			original_read->set_address(addr);
			ongoing_reads.erase(ongoing);
			delete ongoing;
			scheduler->schedule_event(original_read);
		}
	}
}



void LSM_FTL::write(Event *event)
{
	if (event->is_original_application_io()) {
		buffer.addresses.insert(event->get_logical_address());
		if (buffer.addresses.size() >= buffer_threshold) {
			flush(event->get_current_time());
		}
	}
	scheduler->schedule_event(event);
}

void LSM_FTL::register_write_completion(Event const& event, enum status result) {
	collect_stats(event);
	page_mapping.register_write_completion(event, result);
	if (event.is_original_application_io()) {
		return;
	}

	if (event.is_mapping_op()) {
		for (auto& m : merges) {
			if (m->check_write(event) && m->is_finished()) {
				finish_merge(m);
				//print();
				//printf("mapping writes: %d\n", stats.num_mapping_writes);
				//stats.num_mapping_writes = 0;
				//printf("finished merge\n");
			}
		}

		for (auto& run : mapping_tree.runs) {
			// end of flush
			if (run->being_created && run->level == 1 && run->executing_ios.count(event.get_application_io_id())) {
				run->executing_ios.erase(event.get_application_io_id());
				run->being_created = false;
				print();
				//printf("mapping writes: %d\n", stats.num_mapping_writes);
				//stats.num_mapping_writes = 0;
				check_if_should_merge(event.get_current_time());
				flush_in_progress = false;

			}
		}
	}
}


void LSM_FTL::trim(Event *event)
{
	// For now we don't handle trims for DFTL
	assert(false);
}

void LSM_FTL::register_trim_completion(Event & event) {
	page_mapping.register_trim_completion(event);
}

long LSM_FTL::get_logical_address(uint physical_address) const {
	return page_mapping.get_logical_address(physical_address);
}

Address LSM_FTL::get_physical_address(uint logical_address) const {
	return page_mapping.get_physical_address(logical_address);
}

void LSM_FTL::set_replace_address(Event& event) const {
	page_mapping.set_replace_address(event);
}

void LSM_FTL::set_read_address(Event& event) const {
	page_mapping.set_read_address(event);
}

// used for debugging
void LSM_FTL::print() const {
	int total_pages = 0;
	for (auto& run : mapping_tree.runs) {
		printf("level: %d   num pages: %d    id: %d    starting: %d   ending %d\t", run->level, run->mapping_pages.size(), run->id, run->starting_logical_address, run->ending_logical_address);
		if (run->being_created) {
			printf("being created\t");
		}
		if (run->being_merged) {
			printf("being merged\t");
		}
		if (run->obsolete) {
			printf("obsolete \t");
			for (auto i : run->executing_ios) {
				printf("%d ", i);
			}
		}
		printf("\n");
		total_pages += run->mapping_pages.size();
	}
	printf("total pages: %d\n", total_pages);
	printf("\n");
}

void LSM_FTL::print_detailed() const {
	for (auto& run : mapping_tree.runs) {
		printf("level: %d   num pages: %d    id: %d    starting: %d   ending %d\n", run->level, run->mapping_pages.size(), run->id, run->starting_logical_address, run->ending_logical_address);
		//printf("\t");
		for (auto& page : run->mapping_pages) {
			printf("\t%d-%d\n", *page->addresses.begin(), *page->addresses.rbegin());
		}
		//printf("\n");
	}
	printf("\n");
}

long LSM_FTL::find_prospective_address_for_new_run(int size) const {
	int prospective_addr = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1;
	bool found = true;
	do {
		found = true;
		for (auto& mapping_run : mapping_tree.runs) {
			// if prospective adress is
			bool try1 = prospective_addr >= mapping_run->starting_logical_address;
			if ((prospective_addr >= mapping_run->starting_logical_address && mapping_run->ending_logical_address >= prospective_addr)
			 || (prospective_addr + size - 1 >= mapping_run->starting_logical_address && mapping_run->ending_logical_address >= prospective_addr + size - 1 ) ) {
				prospective_addr = mapping_run->ending_logical_address + 1;
				found = false;
				break;
			}
		}

	} while (!found);

	return prospective_addr;
}

bool LSM_FTL::mapping_run::contains(int addr) {
	for (auto& m : mapping_pages) {
		if (addr >= m->first_key && addr <= m->last_key) {
			return m->addresses.count(addr);
		}
	}
	return false;
}

void LSM_FTL::mapping_run::create_bloom_filter() {
	bloom_parameters params;
	params.projected_element_count = buffer_threshold;
	params.false_positive_probability = 0.01;
	params.compute_optimal_parameters();
	filter = bloom_filter(params);
	for (auto& page : mapping_pages) {
		for (int b : page->addresses) {
			filter.insert(b);
		}
	}
}

void LSM_FTL::flush(double time) {
	if (flush_in_progress) {
		return;
	}
	flush_in_progress = true;
	// divide how many IOs to issue
	long prospective_addr = find_prospective_address_for_new_run(1);

	Event* event = new Event(WRITE, prospective_addr, 1, time);
	event->set_mapping_op(true);
	scheduler->schedule_event(event);

	mapping_run* run = new mapping_run();
	run->starting_logical_address = prospective_addr;
	run->ending_logical_address = prospective_addr;
	run->level = 1;
	run->being_merged = false;
	run->being_created = true;
	run->obsolete = false;
	run->executing_ios.insert(event->get_application_io_id());
	mapping_page* p = new mapping_page();
	p->addresses = buffer.addresses;
	p->first_key = *p->addresses.begin();
	p->last_key = *p->addresses.rbegin();
	buffer.addresses.clear();
	run->mapping_pages.push_back(p);
	mapping_tree.runs.push_back(run);
	run->create_bloom_filter();
}

bool LSM_FTL::merge::check_read(Event const& event, IOScheduler *scheduler) {
	for (auto& pages : pages_to_read) {
		if (pages.second.front() == event.get_logical_address()) {
			pages.second.pop();
			if (pages.second.size() > 0) {
				Event* read = new Event(READ, pages.second.front(), 1, event.get_current_time());
				read->set_mapping_op(true);
				scheduler->schedule_event(read);
			}
			Event* write = new Event(WRITE, being_created->starting_logical_address + num_writes_issued, 1, event.get_current_time());
			write->set_mapping_op(true);
			scheduler->schedule_event(write);
			num_writes_issued++;
			return true;
		}
	}
	return false;
}

bool LSM_FTL::merge::check_write(Event const& write) {
	long la = write.get_logical_address();
	if (la >= being_created->starting_logical_address && la <= being_created->ending_logical_address) {
		num_writes_finished++;
		return true;
	}
	return false;
}

void LSM_FTL::erase_run(mapping_run* run) {
	if (run->executing_ios.size() == 0 && run->obsolete) {
		for (auto& page : run->mapping_pages) {
			delete page;
		}
		mapping_tree.runs.erase(std::find(mapping_tree.runs.begin(), mapping_tree.runs.end(), run));
	}
}

void LSM_FTL::finish_merge(merge* m) {
	merges.erase(std::find(merges.begin(), merges.end(), m));
	m->being_created->being_created = false;
	for (auto& run : m->runs) {
		run->obsolete = true;
		run->being_merged = false;
		erase_run(run);
	}
}

void LSM_FTL::check_if_should_merge(double time) {
	map<int, int> levels;
	map<int, int> levels_sizes;
	for (auto& mapping_run : mapping_tree.runs) {
		if (!mapping_run->being_created && !mapping_run->being_merged && !mapping_run->obsolete) {
			levels[mapping_run->level]++;
			levels_sizes[mapping_run->level] += mapping_run->mapping_pages.size();
		}
	}

	if (levels.at(1) < 2) {
		return;
	}
	int highest_level_to_merge = 1;
	int total_pages = 0;
	for (auto& level : levels_sizes) {
		total_pages += level.second;
		if (level.first == highest_level_to_merge && total_pages >= pow(SIZE_RATIO, level.first)) {
			highest_level_to_merge++;
		}
		else {
			break;
		}
	}

	merge* m = new merge();
	merges.push_back(m);
	for (auto& mapping_run : mapping_tree.runs) {
		if (!mapping_run->being_created && !mapping_run->being_merged && !mapping_run->obsolete && (highest_level_to_merge >= mapping_run->level || mapping_run->level == 1)) {
			m->runs.push_back(mapping_run);
		}
	}

	mapping_run* run = new mapping_run();
	m->being_created = run;
	run->being_merged = false;
	run->being_created = true;
	run->obsolete = false;
	//run->executing_ios.insert(event->get_application_io_id());


	set<long> addresses_set;
	for (auto& run : m->runs) {
		run->being_merged = true;
		for (auto& page : run->mapping_pages) {
			addresses_set.insert(page->addresses.begin(), page->addresses.end());
		}
	}

	vector<long> addresses;
	addresses.insert(addresses.begin(), addresses_set.begin(), addresses_set.end());
	vector<mapping_page*> mapping_pages_of_new_run;
	int steps = 0;
	do	{
		mapping_page* mp = new mapping_page();
		run->mapping_pages.push_back(mp);
		if ((steps + 1) * buffer_threshold < addresses.size()) {
			mp->addresses.insert(addresses.begin() + steps * buffer_threshold, addresses.begin() + (steps + 1) * buffer_threshold);
			//addresses.erase(addresses.begin(), addresses.begin() + buffer_threshold);
		}
		else {
			mp->addresses.insert(addresses.begin(), addresses.end());
			//addresses.erase(addresses.begin(), addresses.end());
		}
		mp->first_key = *mp->addresses.begin();
		mp->last_key = *mp->addresses.rbegin();
		steps++;
	} while (steps * buffer_threshold < addresses.size());
	run->create_bloom_filter();

	run->level = floor(log10(run->mapping_pages.size()) / log10(SIZE_RATIO)) + 1;
	/*if (run->id == 9695) {
		print();
		PRINT_LEVEL = 1;
		int starting_address = find_prospective_address_for_new_run(run->mapping_pages.size());
	}*/

	int starting_address = find_prospective_address_for_new_run(run->mapping_pages.size());
	run->starting_logical_address = starting_address;
	run->ending_logical_address = starting_address + run->mapping_pages.size() - 1;
	assert(run->starting_logical_address < NUMBER_OF_ADDRESSABLE_PAGES() + 1);
	assert(run->ending_logical_address < NUMBER_OF_ADDRESSABLE_PAGES() + 1);
	mapping_tree.runs.push_back(run);

	for (auto& run : m->runs) {
		for (int i = run->starting_logical_address; i <= run->ending_logical_address; i++) {
			m->pages_to_read[run].push(i);
		}
		Event* read = new Event(READ, run->starting_logical_address, 1, time);
		read->set_mapping_op(true);
		Address physical_addr_of_translation_page = page_mapping.get_physical_address(run->starting_logical_address);
		read->set_address(physical_addr_of_translation_page);
		scheduler->schedule_event(read);
	}
}


