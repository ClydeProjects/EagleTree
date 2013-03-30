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
	ram(),
	data((Package *) malloc(SSD_SIZE * sizeof(Package))),
	last_io_submission_time(0.0),
	os(NULL)
{
	if(data == NULL){
		fprintf(stderr, "Ssd error: %s: constructor unable to allocate Package data\n", __func__);
		exit(MEM_ERR);
	}
	for (uint i = 0; i < SSD_SIZE; i++)
	{
		(void) new (&data[i]) Package(PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE*i);
	}
	this->getPackages();
	
	// Check for 32bit machine. We do not allow page data on 32bit machines.
	if (PAGE_ENABLE_DATA == 1 && sizeof(void*) == 4)
	{
		fprintf(stderr, "Ssd error: %s: The simulator requires a 64bit kernel when using data pages. Disabling data pages.\n", __func__);
		exit(MEM_ERR);
	}

	if (PAGE_ENABLE_DATA)
	{
		/* Allocate memory for data pages */
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
	}

	assert(VIRTUAL_BLOCK_SIZE > 0);
	assert(VIRTUAL_PAGE_SIZE > 0);

	ftl = new FtlImpl_Page(*this);
	scheduler = new IOScheduler();
	Block_manager_parent* bm;

	switch ( BLOCK_MANAGER_ID ) {
		case 1: bm = new Shortest_Queue_Hot_Cold_BM(); break;
		case 2: bm = new Wearwolf(); break;
		case 3: bm = new Block_manager_parallel(); break;
		case 4: bm = new Block_manager_roundrobin(); break;
		default: bm = new Block_manager_parallel(); break;
	}

	ftl->set_scheduler(scheduler);
	bm->set_all(this, ftl, scheduler);
	scheduler->set_all(this, ftl, bm);

	VisualTracer::init();
	StateVisualiser::init(this);
	StatisticsGatherer::init(this);
	SsdStatisticsExtractor::init(this);

	Event::reset_id_generators(); // reset id generator
}

Ssd::~Ssd(void)
{
	//VisualTracer::get_instance()->print_horizontally(2000);
	if (!scheduler->is_empty()) {
		scheduler->execute_soonest_events();
	}
	//if (PRINT_LEVEL >= 1) StatisticsGatherer::get_global_instance()->print();
	for (uint i = 0; i < SSD_SIZE; i++)	{
		data[i].~Package();
	}
	free(data);

	if (PAGE_ENABLE_DATA) {
		ulong pageSize = ((ulong)(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE)) * (ulong)PAGE_SIZE;
		munmap(page_data, pageSize);
	}
	delete ftl;
	delete scheduler;
}

void Ssd::event_arrive(Event* event) {
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


	if(event->get_event_type() 		== READ) 	ftl->read(event);
	else if(event->get_event_type() == WRITE) 	ftl->write(event);
	else if(event->get_event_type() == TRIM) 	ftl->trim(event);
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
	event_arrive(event);
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
	if (os != NULL && event->is_original_application_io()) {
		os->register_event_completion(event);
	} else {
		delete event;
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
	assert(data != NULL && event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].read(event);
}

enum status Ssd::write(Event &event)
{
	assert(data != NULL && event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
	return data[event.get_address().package].write(event);
}


enum status Ssd::erase(Event &event)
{
	assert(data != NULL && event.get_address().package < SSD_SIZE && event.get_address().valid >= PACKAGE);
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
		//ssd.ram.write(*event);2w
	}
	else if(event -> get_event_type() == WRITE) {
		data[package].lock(event->get_current_time(), 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY, *event);
		//ssd.ram.write(*event);
		//ssd.ram.read(*event);
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

FtlParent& Ssd::get_ftl() const {
	return *ftl;
}
