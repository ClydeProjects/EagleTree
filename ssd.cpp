#include <cmath>
#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

using namespace ssd;

// configure the SSD
Ssd::Ssd():
	data(),
	last_io_submission_time(0.0),
	os(NULL),
	large_events_map(),
	ftl(NULL)
{
	for(uint i = 0; i < SSD_SIZE; i++) {
		int a = PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE * i;
		Package p = Package(a);
		data.push_back(p);
	}

	int sram_allocation = SRAM;

	StatisticsGatherer::init();
	Block_manager_parent* bm = Block_manager_parent::get_new_instance();
	Garbage_Collector* gc = NULL;
	Migrator* migrator = new Migrator();

	if (ftl == NULL) {
		switch (FTL_DESIGN) {
		case 0: ftl = new FtlImpl_Page(this, bm); break;
		case 1: ftl = new DFTL(this, bm); break;
		case 2: ftl = new FAST(this, bm, migrator); break;
		default: ftl = new FtlImpl_Page(this, bm); break;
		}
	}

	scheduler = new IOScheduler();

	Free_Space_Meter::init();
	Free_Space_Per_LUN_Meter::init();

	if (gc == NULL) {
		switch (GARBAGE_COLLECTION_POLICY) {
		case 0: gc = new Garbage_Collector_Greedy(this, bm); break;
		case 1: gc = new Garbage_Collector_LRU(this, bm); break;
		default: gc = new Garbage_Collector_Greedy(this, bm); break;
		}
	}

	ftl->set_scheduler(scheduler);
	gc->set_scheduler(scheduler);

	Wear_Leveling_Strategy* wl = new Wear_Leveling_Strategy(this, migrator);

	bm->init(this, ftl, scheduler, gc, wl, migrator);
	scheduler->init(this, ftl, bm, migrator);
	migrator->init(scheduler, bm, gc, wl, ftl, this);

	StateVisualiser::init(this);

	SsdStatisticsExtractor::init(this);
	Utilization_Meter::init();
	Event::reset_id_generators();
}

Ssd::~Ssd()
{
	execute_all_remaining_events();
	delete ftl;
	delete scheduler;
}

void Ssd::execute_all_remaining_events() {
	while (!scheduler->is_empty()) {
		scheduler->execute_soonest_events();
	}
}

void Ssd::submit(Event* event) {
	if (event->get_ssd_submission_time() + 0.00001 < last_io_submission_time) {
		fprintf(stderr, "Error: Submission time of event (%f) less than last IO submission time (%f).\n", event->get_ssd_submission_time(), last_io_submission_time);
		fprintf(stderr, "Triggering event: ");
		event->print(stderr);
		throw;
	}

	event->set_original_application_io(true);

	// If the IO spans several flash pages, we break it into multiple flash page IOs
	// When these page IOs are all finished, we return to the OS
	static int ssd_id_generator = 0;
	if (event->get_size() > 1 && event->get_tag() == UNDEFINED) {
		int ssd_id = ssd_id_generator++;
		event->set_ssd_id(ssd_id);
		large_events_map.resiger_large_event(event);
		for (int i = 0; i < event->get_size(); i++) {
			Event* e = new Event(*event);
			e->set_application_io_id(ssd_id_generator++);
			e->set_ssd_id(ssd_id);
			e->set_size(1);
			e->set_logical_address(event->get_logical_address() + i);
			submit_to_ftl(e);
		}
	}
	else {
		submit_to_ftl(event);
	}
}

void Ssd::submit_to_ftl(Event* event) {
	if(event->get_event_type() 		== READ) 		ftl->read(event);
	else if(event->get_event_type() == WRITE) 		ftl->write(event);
	else if(event->get_event_type() == TRIM) 		ftl->trim(event);
	else if(event->get_event_type() == MESSAGE) 	scheduler->schedule_event(event);
}

void Ssd::io_map::resiger_large_event(Event* e) {
	event_map[e->get_ssd_id()] = e;
	assert(event_map.count(e->get_ssd_id()));
	io_counter[e->get_ssd_id()] = 0;
}

void Ssd::io_map::register_completion(Event* e) {
	io_counter[e->get_ssd_id()]++;
}

bool Ssd::io_map::is_part_of_large_event(Event* e) {
	return event_map.count(e->get_ssd_id()) == 1;
}

bool Ssd::io_map::is_finished(int id) const {
	Event* orig = event_map.at(id);
	return io_counter.at(id) == orig->get_size();
}

Event* Ssd::io_map::get_original_event(int id) {
	Event* orig = event_map.at(id);
	event_map.erase(id);
	io_counter.erase(id);
	return orig;
}

void Ssd::progress_since_os_is_waiting() {
	scheduler->execute_soonest_events();
}

void Ssd::register_event_completion(Event * event) {
	if (event->is_original_application_io() && !event->get_noop() && !event->is_cached_write() && (event->get_event_type() == WRITE || event->get_event_type() == READ_TRANSFER)) {
		last_io_submission_time = max(last_io_submission_time, event->get_ssd_submission_time());
	}
	if (event->get_event_type() == READ_COMMAND) {
		delete event;
		return;
	}

	if (os == NULL || !event->is_original_application_io()) {
		delete event;
		return;
	}

	// Check if the completed page IO is a part of a big IO that spans multiple pages.
	if (large_events_map.is_part_of_large_event(event)) {
		large_events_map.register_completion(event);
		if (large_events_map.is_finished(event->get_ssd_id())) {
			Event* orig = large_events_map.get_original_event(event->get_ssd_id());
			orig->incr_accumulated_wait_time(event->get_current_time() - orig->get_current_time());
			orig->incr_pure_ssd_wait_time(event->get_current_time() - orig->get_current_time());
			delete event;
			os->register_event_completion(orig);
		} else {
			delete event;
		}
	}
	else {
		os->register_event_completion(event);
	}
}

void Ssd::set_operating_system(OperatingSystem* new_os) {
	os = new_os;
}

double Ssd::get_currently_executing_operation_finish_time(int package) {
	return data[package].get_currently_executing_operation_finish_time();
}

enum status Ssd::issue(Event *event) {
	Package& p = data[event->get_address().package];
	if(event -> get_event_type() == READ_COMMAND) {
		p.lock(event->get_current_time(), BUS_CTRL_DELAY, *event);
		p.read(*event);
	}
	else if(event -> get_event_type() == READ_TRANSFER) {
		p.lock(event->get_current_time(), BUS_CTRL_DELAY + BUS_DATA_DELAY, *event);
	}
	else if(event -> get_event_type() == WRITE) {
		p.lock(event->get_current_time(), 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY, *event);
		data[event->get_address().package].write(*event);
		return SUCCESS;
	}
	else if(event -> get_event_type() == COPY_BACK) {
		p.lock(event->get_current_time(), BUS_CTRL_DELAY, *event);
		p.write(*event);
	}
	else if(event -> get_event_type() == ERASE) {
		p.lock(event -> get_current_time(), BUS_CTRL_DELAY, *event);
		p.erase(*event);
	}
	return SUCCESS;
}

FtlParent* Ssd::get_ftl() const {
	return ftl;
}
