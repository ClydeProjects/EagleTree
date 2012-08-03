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
	time_taken(0.0),
	bus_wait_time(0.0),
	type(type),
	logical_address(logical_address),
	size(size),
	payload(NULL),
	next(NULL),
	noop(false),
	id(id_generator++),
	application_io_id(application_io_id_generator++),
	garbage_collection_op(false),
	mapping_op(false),
	original_application_io(false)
{
	assert(start_time >= 0.0);
	if (VIRTUAL_PAGE_SIZE == 1)
		assert((long long int) logical_address <= (long long int) SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);
	else
		assert((long long int) logical_address*VIRTUAL_PAGE_SIZE <= (long long int) SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

}

Event::Event() : type(NOT_VALID) {}

/*
Event::Event(enum event_type type, ulong logical_address, uint size, double start_time, uint hi)
{
	Event(type, logical_address, size, start_time);
	application_io_id = hi;
	assert(start_time >= 0.0);
}*/

Event::~Event(void)
{
}

/* find the last event in the list to finish and use that event's finish time
 * 	to calculate time_taken
 * add bus_wait_time for all events in the list to bus_wait_time
 * all events in the list do not need to start at the same time
 * bus_wait_time can potentially exceed time_taken with long event lists
 * 	because bus_wait_time is a sum while time_taken is a max
 * be careful to only call this method once when the metaevent is finished */
void Event::consolidate_metaevent(Event &list)
{
	Event *cur;
	double max;
	double tmp;

	assert(start_time >= 0);

	/* find max time taken with respect to this event's start_time */
	max = start_time - list.start_time + list.time_taken;
	for(cur = list.next; cur != NULL; cur = cur -> next)
	{
		tmp = start_time - cur -> start_time + cur -> time_taken;
		if(tmp > max)
			max = tmp;
		bus_wait_time += cur -> get_bus_wait_time();
	}
	time_taken = max;

	assert(time_taken >= 0);
	assert(bus_wait_time >= 0);
}

ulong Event::get_logical_address(void) const
{
	return logical_address;
}

uint Event::get_id(void) const
{
	return id;
}

const Address &Event::get_address(void) const
{
	return address;
}

const Address &Event::get_merge_address(void) const
{
	return merge_address;
}

const Address &Event::get_log_address(void) const
{
	return log_address;
}

const Address &Event::get_replace_address(void) const
{
	return replace_address;
}

void Event::set_log_address(const Address &address)
{
	log_address = address;
}

uint Event::get_size(void) const
{
	return size;
}

enum event_type Event::get_event_type(void) const
{
	return type;
}

void Event::set_event_type(const enum event_type &type)
{
	this->type = type;
}

double Event::get_start_time(void) const
{
	assert(start_time >= 0.0);
	return start_time;
}

double Event::get_time_taken(void) const
{
	assert(time_taken >= 0.0);
	return time_taken;
}

double Event::get_current_time(void) const {
	return start_time + time_taken;
}

uint Event::get_application_io_id(void) const {
	return application_io_id;
}

double Event::get_bus_wait_time(void) const
{
	assert(bus_wait_time >= 0.0);
	return bus_wait_time;
}

bool Event::get_noop(void) const
{
	return noop;
}

Event *Event::get_next(void) const
{
	return next;
}

void Event::set_payload(void *payload)
{
	this->payload = payload;
}

void *Event::get_payload(void) const
{
	return payload;
}

void Event::set_address(const Address &address)
{
	if (type == WRITE || type == READ || type == READ_COMMAND || type == READ_TRANSFER) {
		if (address.valid != PAGE) {
			int i = 0;
			i++;
		}
		//assert(address.valid == PAGE);
	}
	this -> address = address;
}

void Event::set_merge_address(const Address &address)
{
	merge_address = address;
}

void Event::set_replace_address(const Address &address)
{
	replace_address = address;
}

void Event::set_noop(bool value)
{
	noop = value;
}

bool Event::is_original_application_io(void) const {
	return original_application_io;
}

void Event::set_original_application_io(bool val) {
	original_application_io = val;
}

void Event::set_application_io_id(uint value) {
	application_io_id = value;
}

void Event::set_garbage_collection_op(bool value) {
	garbage_collection_op = value;
}

void Event::set_mapping_op(bool value) {
	mapping_op = value;
}

bool Event::is_garbage_collection_op() const {
	return garbage_collection_op;
}

bool Event::is_mapping_op() const {
	return mapping_op;
}

void Event::set_start_time(double value) {
	assert(value > 0);
	start_time = value;
}

void Event::set_next(Event &next)
{
	this -> next = &next;
}

double Event::incr_bus_wait_time(double time_incr)
{
	if(time_incr > 0.0)
		bus_wait_time += time_incr;
	return bus_wait_time;
}

double Event::incr_time_taken(double time_incr)
{
  	if(time_incr > 0.0)
		time_taken += time_incr;
	return time_taken;
}

void Event::print(FILE *stream) const
{
	if(type == READ)
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
	else
		fprintf(stream, "Unknown event type: ");

	fprintf(stream, "%d\t", logical_address);
	address.print(stream);
	if(type == MERGE)
		merge_address.print(stream);
	if(type == WRITE)
		replace_address.print(stream);
	fprintf(stream, " Time[%d, %d, %d]", (int)start_time, (int)(start_time + time_taken), (int)bus_wait_time);
	fprintf(stream, " ID: %d ", id);
	fprintf(stream, " appID: %d", application_io_id);

	if (garbage_collection_op) {
		fprintf(stream, " GC");
	} else if (mapping_op)  {
		fprintf(stream, " MAPPING");
	}
	fprintf(stream, "\n");
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
