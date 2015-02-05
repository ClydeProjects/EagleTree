#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

uint Event::id_generator = 0;
uint Event::application_io_id_generator = 0;

/* see "enum event_type" in ssd.h for details on event types
 * The logical address and size are both measured in flash pages
 * */
Event::Event(enum event_type type, ulong logical_address, uint size, double start_time):
	start_time(start_time),
	execution_time(0.0),
	bus_wait_time(0.0),
	os_wait_time(0.0),
	type(type),
	logical_address(logical_address),
	size(size),
	payload(NULL),
	//next(NULL),
	noop(false),
	id(id_generator++),
	application_io_id(application_io_id_generator++),
	garbage_collection_op(false),
	wear_leveling_op(false),
	mapping_op(false),
	original_application_io(false),
	age_class(0),
	tag(-1),
	accumulated_wait_time(0),
	thread_id(UNDEFINED),
	pure_ssd_wait_time(0),
	copyback(false),
	cached_write(false),
	num_iterations_in_scheduler(0),
	ssd_id(UNDEFINED)
{

	if (application_io_id == 1693276) {
		int i = 0;
		i++;
	}
	assert(start_time >= 0.0);
	if (logical_address > NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE) {
		printf("invalid logical address, too big  %d   %d\n", logical_address, NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE);
		assert(false);
	}
}

Event::Event(Event const& event) :
	start_time(event.start_time),
	execution_time(event.execution_time),
	bus_wait_time(event.bus_wait_time),
	os_wait_time(0.0),
	type(event.type),
	logical_address(event.logical_address),
	size(event.size),
	payload(NULL),
	//next(NULL),
	noop(event.noop),
	id(id_generator++),
	application_io_id(event.application_io_id),
	garbage_collection_op(event.garbage_collection_op),
	wear_leveling_op(event.wear_leveling_op),
	mapping_op(event.mapping_op),
	original_application_io(event.original_application_io),
	age_class(event.age_class),
	tag(event.tag),
	accumulated_wait_time(0),
	thread_id(event.thread_id),
	pure_ssd_wait_time(event.pure_ssd_wait_time),
	copyback(event.copyback),
	cached_write(event.cached_write),
	num_iterations_in_scheduler(0),
	ssd_id(event.ssd_id)
{}

bool Event::is_flexible_read() {
	return dynamic_cast<Flexible_Read_Event*>(this) != NULL;
}

Event::Event() : type(NOT_VALID) {}

void Event::print(FILE *stream) const
{
	if (type == NOT_VALID)
		fprintf(stream, "<NOT VALID> ");
	else if(type == READ)
		fprintf(stream, "R ");
	else if(type == READ_COMMAND)
		fprintf(stream, "C ");
	else if(type == READ_TRANSFER)
		fprintf(stream, "T ");
	else if(type == WRITE)
		fprintf(stream, "W ");
	else if(type == ERASE)
		fprintf(stream, "E ");
	else if(type == MERGE)
		fprintf(stream, "M ");
	else if (type == TRIM)
		fprintf(stream, "D ");
	else if (type == GARBAGE_COLLECTION)
		fprintf(stream, "GC ");
	else if (type == COPY_BACK)
		fprintf(stream, "CB ");
	else
		fprintf(stream, "Unknown event type: ");

	fprintf(stream, "%d\t", logical_address);
	if (type != TRIM) {
		address.print(stream);
	} else {
		replace_address.print(stream);
	}
	if (type == WRITE) {
		replace_address.print(stream);
	}
	//if(type == MERGE)
		//merge_address.print(stream);
	//if(type == WRITE || type == TRIM || type == COPY_BACK)
		//replace_address.print(stream);
	//fprintf(stream, " Time[%f, %f, %f, %f, %f, %f]", start_time, os_wait_time, accumulated_wait_time, bus_wait_time, execution_time, get_current_time());
	fprintf(stream, " Time[%d, %d, %d, %d]", (int)start_time, (int)os_wait_time, (int)bus_wait_time, (int)execution_time);
	//fprintf(stream, " Time[%d, %d, %d]", (int)start_time, (int)bus_wait_time, (int)get_current_time());
	//fprintf(stream, "\tTime[%d, %d, %d, %d]", (int)start_time, (int) (start_time + os_wait_time),(int) bus_wait_time + (int)os_wait_time, (int) get_current_time());
	fprintf(stream, " ID: %d ", id);
	fprintf(stream, " appID: %d", application_io_id);

	if(thread_id != UNDEFINED) {
		fprintf(stream, " thread: %d", thread_id);
	}
	if (garbage_collection_op) {
		fprintf(stream, " GC");
	}
	if (mapping_op)  {
		fprintf(stream, " MAPPING");
	}
	if (wear_leveling_op) {
		fprintf(stream, " WL");
	}
	if (original_application_io) {
		fprintf(stream, " APP");
	}
	if (noop) {
		fprintf(stream, " NOOP");
	}
	if (type == GARBAGE_COLLECTION) {
		fprintf(stream, " age class: %d", age_class);
	}
	if (tag != UNDEFINED) {
		fprintf(stream, " tag: %d", tag);
	}
	fprintf(stream, "\n");
}

void Event::reset_id_generators() {
	Event::id_generator = 0;
	Event::application_io_id_generator = 0;
}
