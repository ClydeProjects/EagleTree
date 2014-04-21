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

Ssd::Ssd():
	data(),
	last_io_submission_time(0.0),
	os(NULL),
	large_events_map()
{
	for(uint i = 0; i < SSD_SIZE; i++) {
		int a = PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE * i;
		Package p = Package(a);
		data.push_back(p);
	}
	
	// Check for 32bit machine. We do not allow page data on 32bit machines.
	/*if (PAGE_ENABLE_DATA == 1 && sizeof(void*) == 4)
	{
		fprintf(stderr, "Ssd error: %s: The simulator requires a 64bit kernel when using data pages. Disabling data pages.\n", __func__);
		exit(MEM_ERR);
	}*/

	/*if (PAGE_ENABLE_DATA)
	{
		ulong pageSize = ((ulong)(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)) * (ulong)PAGE_SIZE;
		page_data = mmap64(NULL, pageSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1 ,0);

		if (page_data == MAP_FAILED)
		{
			fprintf(stderr, "Ssd error: %s: constructor unable to allocate page data.\n", __func__);
			switch (errno)
			{
			case EACCES:
				break;
			}
			printf("%i\n",errno);
			exit(MEM_ERR);
		}
	}*/

	Block_manager_parent* bm = Block_manager_parent::get_new_instance();
	Migrator* migrator = new Migrator();

	if (FTL_DESIGN == 0) {
		ftl = new FtlImpl_Page(this, bm);
	} else if (FTL_DESIGN == 1) {
		ftl = new DFTL(this, bm);
	} else {
		ftl = new FAST(this, bm, migrator);
	}

	scheduler = new IOScheduler();

	Free_Space_Meter::init();
	Free_Space_Per_LUN_Meter::init();

	ftl->set_scheduler(scheduler);

	Garbage_Collector* gc = new Garbage_Collector(this, bm);
	Wear_Leveling_Strategy* wl = new Wear_Leveling_Strategy(this, migrator);

	bm->init(this, ftl, scheduler, gc, wl, migrator);
	scheduler->init(this, ftl, bm, migrator);
	migrator->init(scheduler, bm, gc, wl, ftl, this);

	StateVisualiser::init(this);
	StatisticsGatherer::init();
	SsdStatisticsExtractor::init(this);
	Utilization_Meter::init();
	Event::reset_id_generators();
}

Ssd::~Ssd()
{
	execute_all_remaining_events();
	//VisualTracer::get_instance()->print_horizontally(2000);
	//if (PRINT_LEVEL >= 1) StatisticsGatherer::get_global_instance()->print()
	/*if (PAGE_ENABLE_DATA) {
		ulong pageSize = ((ulong)(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)) * (ulong)PAGE_SIZE;
		munmap(page_data, pageSize);
	}*/
	delete ftl;
	delete scheduler;
}

void Ssd::execute_all_remaining_events() {
	while (!scheduler->is_empty()) {
		scheduler->execute_soonest_events();
	}

}

void Ssd::submit(Event* event) {
	//printf("submitted: %d\n",  event->get_id());
	// Print error and terminate if start time of event is less than IO submission time
	// assert(event->get_start_time() >= last_io_submission_time);
	if (event->get_ssd_submission_time() + 0.00001 < last_io_submission_time) {
		fprintf(stderr, "Error: Submission time of event (%f) less than last IO submission time (%f).\n", event->get_ssd_submission_time(), last_io_submission_time);
		fprintf(stderr, "Triggering event: ");
		event->print(stderr);
		throw;
	}

	event->set_original_application_io(true);
	//IOScheduler::instance()->finish_all_events_until_this_time(event->get_ssd_submission_time());

	// If the IO spans several flash pages, we break it into multiple flash page IOs
	// When these page IOs are all finished, we return to the OS
	static int ssd_id_generator = 0;
	if (event->get_size() > 1) {
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
	if(event->get_event_type() 		== READ) 	ftl->read(event);
	else if(event->get_event_type() == WRITE) 	ftl->write(event);
	else if(event->get_event_type() == TRIM) 	ftl->trim(event);
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

void Ssd::event_arrive(enum event_type type, ulong logical_address, uint size, double start_time)
{
	event_arrive(type, logical_address, size, start_time, NULL);
}

/* This is the function that will be called by DiskSim
 * Provide the event (request) type (see enum in ssd.h),
 * 	logical_address (page number), size of request in pages, and the start
 * 	time (arrive time) of the request
 * The SSD will process the request and return the time taken to process the
 * 	request.  Remember to use the same time units as in the config file. */
void Ssd::event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer)
{
	Event *event = new Event(type, logical_address , size, start_time);
	event->set_payload(buffer);
	submit(event);
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

/*
 * Returns a pointer to the global buffer of the Ssd.
 * It is up to the user to not read out of bound and only
 * read the intended size. i.e. the page size.
 */
void *Ssd::get_result_buffer()
{
	return global_buffer;
}

/* read write erase and merge should only pass on the event
 * 	the Controller should lock the bus channels
 * technically the Package is conceptual, but we keep track of statistics
 * 	and addresses with Packages, so send Events through Package but do not 
 * 	have Package do anything but update its statistics and pass on to Die */
enum status Ssd::read(Event &event)
{
	assert(event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].read(event);
}

enum status Ssd::write(Event &event)
{
	assert(event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].write(event);
}


enum status Ssd::erase(Event &event)
{
	assert(event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].erase(event);
}

void Ssd::set_operating_system(OperatingSystem* new_os) {
	os = new_os;
}

double Ssd::get_currently_executing_operation_finish_time(int package) {
	return data[package].get_currently_executing_operation_finish_time();
}

enum status Ssd::issue(Event *event) {
	int package = event->get_address().package;
	//if (event->get_logical_address() == 0 && event->get_event_type() != ERASE && event->get_event_type() != READ_COMMAND && event->get_event_type() != READ_TRANSFER) {
		//event->print();
	//}
	if(event -> get_event_type() == READ_COMMAND) {
		data[package].lock(event->get_current_time(), BUS_CTRL_DELAY, *event);
		read(*event);
	}
	else if(event -> get_event_type() == READ_TRANSFER) {
		data[package].lock(event->get_current_time(), BUS_CTRL_DELAY + BUS_DATA_DELAY, *event);
	}
	else if(event -> get_event_type() == WRITE) {
		data[package].lock(event->get_current_time(), 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY, *event);
		write(*event);
		return SUCCESS;
	}
	else if(event -> get_event_type() == COPY_BACK) {
		data[package].lock(event->get_current_time(), BUS_CTRL_DELAY, *event);
		write(*event);
	}
	else if(event -> get_event_type() == ERASE) {
		data[package].lock(event -> get_current_time(), BUS_CTRL_DELAY, *event);
		erase(*event);
	}
	return SUCCESS;
}

FtlParent* Ssd::get_ftl() const {
	return ftl;
}
