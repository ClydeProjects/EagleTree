/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_die.cpp is part of FlashSim. */

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

/* Die class
 * Brendan Tauras 2009-11-03
 *
 * The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Die::Die(Channel &channel, long physical_address):
	data((Plane *) malloc(DIE_SIZE * sizeof(Plane))),
	channel(channel),
	least_worn(0),
	last_erase_time(0.0),
	currently_executing_io_finish_time(0.0),
	last_read_io(-1)
{
	if(data == NULL){
		fprintf(stderr, "Die error: %s: constructor unable to allocate Plane data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < DIE_SIZE; i++)
		(void) new (&data[i]) Plane(PLANE_REG_READ_DELAY, PLANE_REG_WRITE_DELAY, physical_address+(PLANE_SIZE*BLOCK_SIZE*i));
}

Die::~Die(void)
{
	assert(data != NULL);
	for(uint i = 0; i < DIE_SIZE; i++)
		data[i].~Plane();
	free(data);
}

enum status Die::read(Event &event)
{
	assert(data != NULL);
	assert(event.get_address().plane < DIE_SIZE && event.get_address().valid > DIE);
	assert(currently_executing_io_finish_time <= event.get_current_time());
	if (event.get_event_type() == READ_COMMAND) {
		last_read_io = event.get_application_io_id();
	}

	enum status result = data[event.get_address().plane].read(event);
	currently_executing_io_finish_time = event.get_current_time();
	return result;
}

enum status Die::write(Event &event)
{
	assert(data != NULL);
	assert(event.get_address().plane < DIE_SIZE && event.get_address().valid > DIE);
	assert(currently_executing_io_finish_time <= event.get_current_time());
	//last_read_io = event.get_application_io_id();
	enum status result = data[event.get_address().plane].write(event);
	currently_executing_io_finish_time = event.get_current_time();
	return result;
}

/* if no errors
 * 	updates last_erase_time if later time
 * 	updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status Die::erase(Event &event)
{
	assert(data != NULL);
	assert(event.get_address().plane < DIE_SIZE && event.get_address().valid > DIE);

	assert(currently_executing_io_finish_time <= event.get_current_time());

	//last_read_io = event.get_application_io_id();
	enum status status = data[event.get_address().plane].erase(event);
	currently_executing_io_finish_time = event.get_current_time();
	return status;
}

double Die::get_currently_executing_io_finish_time() {
	return currently_executing_io_finish_time;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Die::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > DIE && address.plane < DIE_SIZE)
		return data[address.plane].get_last_erase_time(address);
	else
		return last_erase_time;
}

Block *Die::get_block_pointer(const Address & address)
{
	assert(address.valid >= PLANE);
	return data[address.plane].get_block_pointer(address);
}

int Die::get_last_read_application_io() {
	return last_read_io;
}

bool Die::register_is_busy() {
	return last_read_io != -1;
}

void Die::clear_register() {
	last_read_io = -1;
}
