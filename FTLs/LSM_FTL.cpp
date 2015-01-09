#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <ctgmath>
#include "../ssd.h"

using namespace ssd;



LSM_FTL::LSM_FTL(Ssd *ssd, Block_manager_parent* bm) :
		FtlParent(ssd, bm),
		page_mapping(new FtlImpl_Page(ssd, bm)),
		tree(LSM_Tree_Manager::mapping_tree(NULL, page_mapping))

{
	IS_FTL_PAGE_MAPPING = true;
}

LSM_FTL::LSM_FTL() :
		page_mapping(),
		tree(LSM_Tree_Manager::mapping_tree(NULL, NULL)),
		FtlParent()
{
	IS_FTL_PAGE_MAPPING = true;
}

void LSM_FTL::set_scheduler(IOScheduler* sched) {
	scheduler = tree.scheduler = sched;
}

LSM_FTL::~LSM_FTL(void)
{
}

void LSM_FTL::read(Event *event)
{
	if (event->is_original_application_io()) {
		if (tree.buf.addresses.count(event->get_logical_address()) == 1) {
			scheduler->schedule_event(event);
		}
		else {
			//print_detailed();
			tree.create_ongoing_read(event);
		}
	}

}



void LSM_FTL::register_read_completion(Event const& event, enum status result) {
	collect_stats(event);
	if (event.is_mapping_op()) {
		for (auto& m : tree.merges) {
			m->check_read(event, scheduler);
		}
		LSM_Tree_Manager::ongoing_read* ongoing = NULL;
		for (auto& r : tree.ongoing_reads) {
			if (r->read_ios_submitted.count(event.get_application_io_id())) {
				ongoing = r;
				break;
			}
		}
		if (ongoing == NULL) {
			return;
		}

		int mapping_la = event.get_logical_address();
		LSM_Tree_Manager::mapping_run* run = NULL;
		for (auto& r : tree.runs) {
			if (r->starting_logical_address <= mapping_la && r->ending_logical_address >= mapping_la) {
				run = r;
			}
		}
		if (event.get_application_io_id() == 923085) {
			int i =0;
			i++;
			print();
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

		// this code is meant to erase a run if there were still pending reads to it
		tree.erase_run(run);
		if (!found_address) {
			tree.attend_ongoing_read(ongoing, event.get_current_time());
		}
		else {
			Event* original_read = ongoing->original_read;
			Address addr = page_mapping->get_physical_address(original_read->get_logical_address());
			original_read->set_address(addr);
			tree.ongoing_reads.erase(ongoing);
			delete ongoing;
			scheduler->schedule_event(original_read);
		}
	}
}



void LSM_FTL::write(Event *event)
{
	if (event->is_original_application_io()) {
		tree.buf.addresses.insert(event->get_logical_address());
		if (tree.buf.addresses.size() >= LSM_Tree_Manager::buffer_threshold) {
			tree.flush(event->get_current_time());
		}
	}
	scheduler->schedule_event(event);
}

void LSM_FTL::register_write_completion(Event const& event, enum status result) {
	collect_stats(event);
	page_mapping->register_write_completion(event, result);
	if (event.is_original_application_io()) {
		return;
	}

	if (event.is_mapping_op()) {
		for (auto& m : tree.merges) {
			if (m->check_write(event) && m->is_finished()) {
				tree.finish_merge(m);
				//print();
				//printf("mapping writes: %d\n", stats.num_mapping_writes);
				//stats.num_mapping_writes = 0;
				//printf("finished merge\n");
			}
		}

		for (auto& run : tree.runs) {
			// end of flush
			if (run->being_created && run->level == 1 && run->executing_ios.count(event.get_application_io_id())) {
				run->executing_ios.erase(event.get_application_io_id());
				run->being_created = false;
				//print();
				//printf("mapping writes: %d\n", stats.num_mapping_writes);
				//stats.num_mapping_writes = 0;
				tree.check_if_should_merge(event.get_current_time());
				tree.flush_in_progress = false;

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
	page_mapping->register_trim_completion(event);
}

long LSM_FTL::get_logical_address(uint physical_address) const {
	return page_mapping->get_logical_address(physical_address);
}

Address LSM_FTL::get_physical_address(uint logical_address) const {
	return page_mapping->get_physical_address(logical_address);
}

void LSM_FTL::set_replace_address(Event& event) const {
	page_mapping->set_replace_address(event);
}

void LSM_FTL::set_read_address(Event& event) const {
	page_mapping->set_read_address(event);
}

// used for debugging
void LSM_FTL::print() const {
	tree.print();
}

void LSM_FTL::print_detailed() const {
	for (auto& run : tree.runs) {
		printf("level: %d   num pages: %d    id: %d    starting: %d   ending %d\n", run->level, run->mapping_pages.size(), run->id, run->starting_logical_address, run->ending_logical_address);
		//printf("\t");
		for (auto& page : run->mapping_pages) {
			printf("\t%d-%d\n", *page->addresses.begin(), *page->addresses.rbegin());
		}
		//printf("\n");
	}
	printf("\n");
}


