/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_event.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Event class
 * Brendan Tauras 2010-07-16
 *
 * Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives.
 */

#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

uint Event::id_generator = 0;
uint Event::application_io_id_generator = 0;

/* see "enum event_type" in ssd.h for details on event types */
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
	experiment_io(false),
	thread_id(UNDEFINED),
	pure_ssd_wait_time(0)
{
	assert(start_time >= 0.0);
	if (logical_address > NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE) {
		printf("invalid logical address, too big  %d   %d\n", logical_address, NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE);
		assert(false);
	}
}

Event::Event(Event& event) :
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
	experiment_io(event.experiment_io),
	thread_id(event.thread_id),
	pure_ssd_wait_time(0)
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
	address.print(stream);
	//if(type == MERGE)
		//merge_address.print(stream);
	//if(type == WRITE || type == TRIM || type == COPY_BACK)
		//replace_address.print(stream);
	fprintf(stream, " Time[%d, %d, %d, %d, %d, %d]", (int)start_time, (int)os_wait_time, (int)accumulated_wait_time, (int)bus_wait_time, (int)execution_time, (int)get_current_time());
	//fprintf(stream, " Time[%d, %d, %d]", (int)start_time, (int)bus_wait_time, (int)get_current_time());
	//fprintf(stream, "\tTime[%d, %d, %d, %d]", (int)start_time, (int) (start_time + os_wait_time),(int) bus_wait_time + (int)os_wait_time, (int) get_current_time());
	fprintf(stream, " ID: %d ", id);
	fprintf(stream, " appID: %d", application_io_id);

	if(thread_id != UNDEFINED) {
		fprintf(stream, " thread: %d", thread_id);
	}
	if (garbage_collection_op) {
		fprintf(stream, " GC");
	} else if (mapping_op)  {
		fprintf(stream, " MAPPING");
	}
	if (wear_leveling_op) {
		fprintf(stream, " WL");
	}
	if (original_application_io) {
		fprintf(stream, " APP");
	}
	if (type == GARBAGE_COLLECTION) {
		fprintf(stream, " age class: %d", age_class);
	}
	fprintf(stream, "\n");
}

void Event::reset_id_generators() {
	Event::id_generator = 0;
	Event::application_io_id_generator = 0;
}

#if 0
/* may be useful for further integration with DiskSim */

/* caution: copies pointers from rhs */
ioreq_event &Event::operator= (const ioreq_event &rhs)
{
	assert(&rhs != NULL);
	if((const ioreq_event *) &rhs == (const ioreq_event *) &(this -> ioreq))
		return *(this -> ioreq);
	ioreq -> time = rhs.time;
	ioreq -> type = rhs.type;
	ioreq -> next = rhs.next;
	ioreq -> prev = rhs.prev;
	ioreq -> bcount = rhs.bcount;
	ioreq -> blkno = rhs.blkno;
	ioreq -> flags = rhs.flags;
	ioreq -> busno = rhs.busno;
	ioreq -> slotno = rhs.slotno;
	ioreq -> devno = rhs.devno;
	ioreq -> opid = rhs.opid;
	ioreq -> buf = rhs.buf;
	ioreq -> cause = rhs.cause;
	ioreq -> tempint1 = rhs.tempint1;
	ioreq -> tempint2 = rhs.tempint2;
	ioreq -> tempptr1 = rhs.tempptr1;
	ioreq -> tempptr2 = rhs.tempptr2;
	ioreq -> mems_sled = rhs.mems_sled;
	ioreq -> mems_reqinfo = rhs.mems_reqinfo;
	ioreq -> start_time = rhs.start_time;
	ioreq -> batchno = rhs.batchno;
	ioreq -> batch_complete = rhs.batch_complete;
	ioreq -> batch_size = rhs.batch_size;
	ioreq -> batch_next = rhs.batch_next;
	ioreq -> batch_prev = rhs.batch_prev;
	return *ioreq;
}
#endif
