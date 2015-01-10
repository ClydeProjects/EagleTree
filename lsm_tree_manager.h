/*
 * scheduler.h
 *
 *  Created on: Feb 18, 2013
 *      Author: niv
 */

#ifndef LSM_H_
#define LSM_H_

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <stack>
#include <queue>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>

#include "ssd.h"
#include "block_management.h"
#include "scheduler.h"

namespace ssd {

template <class T, class V> int LSM_Tree_Manager<T, V>::buffer_threshold = 128;
template <class T, class V> int LSM_Tree_Manager<T, V>::SIZE_RATIO = 2;
template <class T, class V> int LSM_Tree_Manager<T, V>::mapping_run::id_generator = 0;

template <class T, class V> bool LSM_Tree_Manager<T, V>::mapping_tree::in_buffer(int element) {
	return buf.addresses.count(element) == 1;
}

template <class T, class V> LSM_Tree_Manager<T, V>::mapping_tree::mapping_tree(IOScheduler* sched, FtlImpl_Page* mapping) :
			flush_in_progress(false), scheduler(sched), page_mapping(mapping), listener(NULL) { }

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::set_listener(LSM_Tree_Manager_Listener<T, V>* l) {
	listener = l;
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::set_scheduler(IOScheduler* s) {
	scheduler = s;
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::insert(int element, double time) {
	buf.addresses[element] = 1;
	if (buf.addresses.size() >= LSM_Tree_Manager::buffer_threshold) {
		flush(time);
	}
}

// used for debugging
template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::print() const {
	int total_pages = 0;
	for (auto& run : runs) {
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

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::register_write_completion(Event const& event) {
	for (auto& m : merges) {
		if (m->check_write(event) && m->is_finished()) {
			finish_merge(m);
			//print();
			//printf("mapping writes: %d\n", stats.num_mapping_writes);
			//stats.num_mapping_writes = 0;
			//printf("finished merge\n");
		}
	}

	for (auto& run : runs) {
		// end of flush
		if (run->being_created && run->level == 1 && run->executing_ios.count(event.get_application_io_id())) {
			run->executing_ios.erase(event.get_application_io_id());
			run->being_created = false;
			//print();
			//printf("mapping writes: %d\n", stats.num_mapping_writes);
			//stats.num_mapping_writes = 0;
			check_if_should_merge(event.get_current_time());
			flush_in_progress = false;
		}
	}
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::create_ongoing_read(int key, V temp_data, double time) {
	LSM_Tree_Manager<T, V>::ongoing_read* r = new LSM_Tree_Manager<T, V>::ongoing_read();
	r->key = key;
	r->temp = temp_data;
	ongoing_reads.insert(r);
	attend_ongoing_read(r, time);
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::register_read_completion(Event const& event) {
	for (auto& m : merges) {
		m->check_read(event, scheduler);
	}
	LSM_Tree_Manager<T, V>::ongoing_read* ongoing = NULL;
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
	LSM_Tree_Manager<T, V>::mapping_run* run = NULL;
	for (auto& r : runs) {
		if (r->starting_logical_address <= mapping_la && r->ending_logical_address >= mapping_la) {
			run = r;
		}
	}
	if (run->executing_ios.count(event.get_application_io_id()) == 0) {
		event.print();
		printf("key: %d\n", ongoing->key);
	}
	assert(run->executing_ios.count(event.get_application_io_id()) == 1);
	run->executing_ios.erase(event.get_application_io_id());
	ongoing->read_ios_submitted.erase(event.get_application_io_id());
	assert(run->executing_ios.count(event.get_application_io_id()) == 0);

	assert(run != NULL);
	bool found_address = false;
	T value;
	int orig_la = ongoing->key;
	for (auto& page : run->mapping_pages) {
		if (page->first_key <= orig_la && page->last_key >= orig_la && page->addresses.count(orig_la) == 1) {
			found_address = true;
			value = page->addresses.at(orig_la);
			break;
		}
	}

	// this code is meant to erase a run if there were still pending reads to it
	erase_run(run);
	if (!found_address) {
		attend_ongoing_read(ongoing, event.get_current_time());
	}
	else {
		ongoing_reads.erase(ongoing);
		delete ongoing;
		if (listener != NULL) {
			listener->event_finished(ongoing->key, value, ongoing->temp);
		}
	}
}

template <class T, class V> long LSM_Tree_Manager<T, V>::mapping_tree::find_prospective_address_for_new_run(int size) const {
	int prospective_addr = NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1;
	bool found = true;
	do {
		found = true;
		for (auto& r : runs) {
			// if prospective adress is
			//bool try1 = prospective_addr >= r->starting_logical_address;
			int end = prospective_addr + size - 1;

			if ((prospective_addr >= r->starting_logical_address && prospective_addr <= r->ending_logical_address) ||
					(end >= r->starting_logical_address && end <= r->ending_logical_address) ||
					(r->starting_logical_address >= prospective_addr && r->starting_logical_address <= end) ||
					(r->ending_logical_address >= prospective_addr && r->ending_logical_address <= end)) {
				prospective_addr = r->ending_logical_address + 1;
				found = false;
				break;
			}

			/*if ((prospective_addr >= r->starting_logical_address && r->ending_logical_address >= prospective_addr)
			 || (end >= r->starting_logical_address && r->ending_logical_address >= end ) ) {
				prospective_addr = r->ending_logical_address + 1;
				found = false;
				break;
			}*/
		}

	} while (!found);

	return prospective_addr;
}

template <class T, class V> bool LSM_Tree_Manager<T, V>::mapping_run::contains(int addr) {
	for (auto& m : mapping_pages) {
		if (addr >= m->first_key && addr <= m->last_key) {
			return m->addresses.count(addr);
		}
	}
	return false;
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_run::create_bloom_filter() {
	bloom_parameters params;
	params.projected_element_count = buffer_threshold;
	params.false_positive_probability = 0.01;
	params.compute_optimal_parameters();
	filter = bloom_filter(params);
	for (auto& page : mapping_pages) {
		for (auto& b : page->addresses) {
			filter.insert(b.first);
		}
	}
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::flush(double time) {
	if (flush_in_progress) {
		return;
	}
	flush_in_progress = true;
	// divide how many IOs to issue
	long prospective_addr = find_prospective_address_for_new_run(1);

	Event* event = new Event(WRITE, prospective_addr, 1, time);
	event->set_mapping_op(true);
	scheduler.schedule_event(event);

	mapping_run* run = new mapping_run();
	run->starting_logical_address = prospective_addr;
	run->ending_logical_address = prospective_addr;
	run->level = 1;
	run->being_merged = false;
	run->being_created = true;
	run->obsolete = false;
	run->executing_ios.insert(event->get_application_io_id());
	mapping_page* p = new mapping_page();
	p->addresses = buf.addresses;
	p->first_key = (*p->addresses.begin()).first;
	p->last_key = (*p->addresses.rbegin()).first;
	buf.addresses.clear();
	run->mapping_pages.push_back(p);
	runs.push_back(run);
	run->create_bloom_filter();
}

// check if the read was a part of a merge operation.
template <class T, class V> bool LSM_Tree_Manager<T, V>::merge::check_read(Event const& event, scheduler_wrapper scheduler) {
	for (auto& pages : pages_to_read) {
		if (pages.second.front() == event.get_logical_address()) {
			pages.second.pop();
			if (pages.second.size() > 0) {
				Event* read = new Event(READ, pages.second.front(), 1, event.get_current_time());
				read->set_mapping_op(true);
				scheduler.schedule_event(read);
			}
			Event* write = new Event(WRITE, being_created->starting_logical_address + num_writes_issued, 1, event.get_current_time());
			write->set_mapping_op(true);
			scheduler.schedule_event(write);
			num_writes_issued++;
			return true;
		}
	}
	return false;
}

template <class T, class V> bool LSM_Tree_Manager<T, V>::merge::check_write(Event const& write) {
	long la = write.get_logical_address();
	if (la >= being_created->starting_logical_address && la <= being_created->ending_logical_address) {
		num_writes_finished++;
		return true;
	}
	return false;
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::erase_run(mapping_run* run) {
	if (run->executing_ios.size() == 0 && run->obsolete) {
		for (auto& page : run->mapping_pages) {
			delete page;
		}
		runs.erase(std::find(runs.begin(), runs.end(), run));
	}
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::finish_merge(merge* m) {
	merges.erase(std::find(merges.begin(), merges.end(), m));
	m->being_created->being_created = false;
	for (auto& run : m->runs) {
		run->obsolete = true;
		run->being_merged = false;
		erase_run(run);
	}
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::check_if_should_merge(double time) {
	map<int, int> levels;
	map<int, int> levels_sizes;
	for (auto& mapping_run : runs) {
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
	for (auto& mapping_run : runs) {
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


	map<int, int> addresses_set;
	for (auto& run : m->runs) {
		run->being_merged = true;
		for (auto& page : run->mapping_pages) {
			addresses_set.insert(page->addresses.begin(), page->addresses.end());
		}
	}

	vector<pair<int, int> > addresses;
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
		mp->first_key = (*mp->addresses.begin()).first;
		mp->last_key = (*mp->addresses.rbegin()).first;
		steps++;
	} while (steps * buffer_threshold < addresses.size());
	run->create_bloom_filter();

	run->level = floor(log10(run->mapping_pages.size()) / log10(SIZE_RATIO)) + 1;

	int starting_address = find_prospective_address_for_new_run(run->mapping_pages.size());
	run->starting_logical_address = starting_address;
	run->ending_logical_address = starting_address + run->mapping_pages.size() - 1;
	assert(run->starting_logical_address < NUMBER_OF_ADDRESSABLE_PAGES() + 1);
	assert(run->ending_logical_address < NUMBER_OF_ADDRESSABLE_PAGES() + 1);

	/*for (auto& r : runs) {
		if ((run->starting_logical_address >= r->starting_logical_address && run->starting_logical_address <= r->ending_logical_address) ||
		    (run->ending_logical_address >= r->starting_logical_address && run->ending_logical_address <= r->ending_logical_address) ||
		    (r->starting_logical_address >= run->starting_logical_address && r->starting_logical_address <= run->ending_logical_address) ||
		    (r->ending_logical_address >= run->starting_logical_address && r->ending_logical_address <= run->ending_logical_address)) {
			print();
			assert(false);
		}
	}*/

	runs.push_back(run);

	for (auto& run : m->runs) {
		for (int i = run->starting_logical_address; i <= run->ending_logical_address; i++) {
			m->pages_to_read[run].push(i);
		}
		Event* read = new Event(READ, run->starting_logical_address, 1, time);
		read->set_mapping_op(true);
		Address physical_addr_of_translation_page = page_mapping->get_physical_address(run->starting_logical_address);
		read->set_address(physical_addr_of_translation_page);
		scheduler.schedule_event(read);
	}
}

template <class T, class V>  void LSM_Tree_Manager<T, V>::mapping_tree::attend_ongoing_read(ongoing_read* r, double time) {
	for (int i = runs.size() - 1; i >= 0; i--) {
		mapping_run* run = runs[i];
		int la = r->key;
		if (!run->being_created && !run->obsolete && r->run_ids_attempted.count(run->id) == 0 && (/*run->contains(la) ||*/ runs[i]->filter.contains(la))) {
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
				scheduler.schedule_event(read);
				return;
			}
		}
	}
}

};


#endif /* LSM_H_ */
