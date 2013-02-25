/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_plane.cpp is part of FlashSim. */

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

/* Plane class
 * Brendan Tauras 2009-11-03
 *
 * The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Plane::Plane(double reg_read_delay, double reg_write_delay, long physical_address):
	data((Block *) malloc(PLANE_SIZE * sizeof(Block))),
	erases_remaining(BLOCK_ERASES),
	last_erase_time(0.0)
{
	if(reg_read_delay < 0.0)
	{  
		fprintf(stderr, "Plane error: %s: constructor received negative register read delay value\n\tsetting register read delay to 0.0\n", __func__);
		reg_read_delay = 0.0;
	}
	else
		this -> reg_read_delay = reg_read_delay;

	if(reg_write_delay < 0.0)
	{  
		fprintf(stderr, "Plane error: %s: constructor received negative register write delay value\n\tsetting register write delay to 0.0\n", __func__);
		reg_write_delay = 0.0;
	}
	else
		this -> reg_write_delay = reg_write_delay;

	/* next page only uses the block, page, and valid fields of the address
	 *    object so we can ignore setting the other fields
	 * plane does not know about higher-level hardware organization, so we cannot
	 *    set the other fields anyway */
	next_page.block = 0;
	next_page.page = 0;
	next_page.valid = PAGE;
	if(data == NULL){
		fprintf(stderr, "Plane error: %s: constructor unable to allocate Block data\n", __func__);
		exit(MEM_ERR);
	}

	for(uint i = 0; i < PLANE_SIZE; i++)
	{
		(void) new (&data[i]) Block(physical_address+(i*BLOCK_SIZE));
	}


	return;
}

Plane::~Plane(void)
{
	assert(data != NULL);
	for(uint i = 0; i < PLANE_SIZE; i++)
		data[i].~Block();
	free(data);
	return;
}

enum status Plane::read(Event &event)
{
	assert(event.get_address().block < PLANE_SIZE && event.get_address().valid > PLANE);
	return data[event.get_address().block].read(event);
}

enum status Plane::write(Event &event)
{
	assert(event.get_address().block < PLANE_SIZE && event.get_address().valid > PLANE && next_page.valid >= BLOCK);
	enum block_state prev = data[event.get_address().block].get_state();
	status s = data[event.get_address().block].write(event);
	return s;
}


/* if no errors
 * 	updates last_erase_time if later time
 * 	updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status Plane::erase(Event &event)
{
	assert(event.get_address().block < PLANE_SIZE && event.get_address().valid > PLANE);
	enum status status = data[event.get_address().block]._erase(event);
	return status;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Plane::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < PLANE_SIZE)
		return data[address.block].get_last_erase_time();
	else
		return last_erase_time;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
ulong Plane::get_erases_remaining(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < PLANE_SIZE)
		return data[address.block].get_erases_remaining();
	else
		return erases_remaining;
}

enum page_state Plane::get_state(const Address &address) const
{  
	assert(data != NULL && address.block < PLANE_SIZE && address.valid >= PLANE);
	return data[address.block].get_state(address);
}

enum block_state Plane::get_block_state(const Address &address) const
{
	assert(data != NULL && address.block < PLANE_SIZE && address.valid >= PLANE);
	return data[address.block].get_state();
}

/* update address to next free page in plane
 * error condition will result in (address.valid < PAGE) */
void Plane::get_free_page(Address &address) const
{
	assert(data[address.block].get_pages_valid() < BLOCK_SIZE);

	address.page = data[address.block].get_pages_valid();
	address.valid = PAGE;
	address.set_linear_address(address.get_linear_address()+ address.page - (address.get_linear_address()%BLOCK_SIZE));
	return;
}

/* internal method to keep track of the next usable (free or active) page in
 *    this plane
 * method is called by write and erase methods and calls Block::get_next_page()
 *    such that the get_free_page method can run in constant time */
enum status Plane::get_next_page(void)
{
	return SUCCESS;

	next_page.valid = PLANE;

	for(uint i = 0; i < PLANE_SIZE; i++)
	{
		if(data[i].get_state() != INACTIVE)
		{
			next_page.valid = BLOCK;
			if(data[i].get_next_page(next_page) == SUCCESS)
			{
				next_page.block = i;
				return SUCCESS;
			}
		}
	}
	return FAILURE;
}

Block *Plane::get_block_pointer(const Address & address)
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pointer();
}

