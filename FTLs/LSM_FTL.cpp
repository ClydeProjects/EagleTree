#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <ctgmath>
#include "../ssd.h"

using namespace ssd;

LSM_FTL::LSM_FTL(Ssd *ssd, Block_manager_parent* bm) :
		flash_resident_page_ftl(ssd, bm),
		tree(LSM_Tree_Manager<int, Event*>::mapping_tree(NULL, page_mapping))
{
	tree.set_listener(this);
	IS_FTL_PAGE_MAPPING = true;
}

LSM_FTL::LSM_FTL() :
		flash_resident_page_ftl(),
		tree(LSM_Tree_Manager<int, Event*>::mapping_tree(NULL, NULL))
{
	tree.set_listener(this);
	IS_FTL_PAGE_MAPPING = true;
}

void LSM_FTL::set_scheduler(IOScheduler* sched) {
	tree.set_scheduler(sched);
	scheduler = sched;
	tree.set_listener(this);
}

LSM_FTL::~LSM_FTL(void)
{
}

void LSM_FTL::read(Event *event) {
	if (event->is_original_application_io()) {
		if (tree.in_buffer(event->get_logical_address() /* or if its in the normal cache */ )) {
			scheduler->schedule_event(event);
		}
		else {
			tree.create_ongoing_read(event->get_logical_address(), event, event->get_current_time());
		}
	}
}

void LSM_FTL::register_read_completion(Event const& event, enum status result) {
	collect_stats(event);
	if (event.is_mapping_op()) {
		tree.register_read_completion(event);
	}
}

void LSM_FTL::write(Event *event) {
	if (event->is_original_application_io()) {
		tree.insert(event->get_logical_address(), 1, event->get_current_time());
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
		tree.register_write_completion(event);
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

/*void LSM_FTL::print_detailed() const {
	for (auto& run : tree.runs) {
		printf("level: %d   num pages: %d    id: %d    starting: %d   ending %d\n", run->level, run->mapping_pages.size(), run->id, run->starting_logical_address, run->ending_logical_address);
		//printf("\t");
		for (auto& page : run->mapping_pages) {
			printf("\t%d-%d\n", *page->addresses.begin(), *page->addresses.rbegin());
		}
		//printf("\n");
	}
	printf("\n");
}*/

bool LSM_FTL::event_finished(bool found, int key, int value, Event* original_read) {
	//printf("event finished   %d   %d   ", key, value);
	//original_read->print();
	Address addr = page_mapping->get_physical_address(original_read->get_logical_address());
	original_read->set_address(addr);
	scheduler->schedule_event(original_read);
	return false;
}
