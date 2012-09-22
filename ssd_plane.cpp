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

Plane::Plane(const Die &parent, uint plane_size, double reg_read_delay, double reg_write_delay, long physical_address):
	size(plane_size),

	/* use a const pointer (Block * const data) to use as an array
	 * but like a reference, we cannot reseat the pointer */
	data((Block *) malloc(size * sizeof(Block))),

	parent(parent),

	/* set erases remaining to BLOCK_ERASES to match Block constructor args */
	erases_remaining(BLOCK_ERASES),

	/* assume hardware created at time 0 and had an implied free erasure */
	last_erase_time(0.0),

	free_blocks(size)
{
	uint i;

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

	/* new cannot initialize an array with constructor args so
	 * 	malloc the array
	 * 	then use placement new to call the constructor for each element
	 * chose an array over container class so we don't have to rely on anything
	 * 	i.e. STL's std::vector */
	/* array allocated in initializer list:
 	 * data = (Block *) malloc(size * sizeof(Block)); */
	if(data == NULL){
		fprintf(stderr, "Plane error: %s: constructor unable to allocate Block data\n", __func__);
		exit(MEM_ERR);
	}

	for(i = 0; i < size; i++)
	{
		(void) new (&data[i]) Block(*this, BLOCK_SIZE, BLOCK_ERASES, BLOCK_ERASE_DELAY,physical_address+(i*BLOCK_SIZE));
	}


	return;
}

Plane::~Plane(void)
{
	assert(data != NULL);
	uint i;
	/* call destructor for each Block array element
	 * since we used malloc and placement new */
	for(i = 0; i < size; i++)
		data[i].~Block();
	free(data);
	return;
}

enum status Plane::read(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE);
	return data[event.get_address().block].read(event);
}

enum status Plane::write(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE && next_page.valid >= BLOCK);

	enum block_state prev = data[event.get_address().block].get_state();

	status s = data[event.get_address().block].write(event);

	//if(event.get_address().block == next_page.block)
		/* if all blocks in the plane are full and this function fails,
		 * the next_page address valid field will be set to PLANE */
		//(void) get_next_page();

	if(prev == FREE && data[event.get_address().block].get_state() != FREE)
		free_blocks--;

	return s;
}

enum status Plane::replace(Event &event)
{
	assert(event.get_address().block < size);
	return data[event.get_replace_address().block].replace(event);
}


/* if no errors
 * 	updates last_erase_time if later time
 * 	updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status Plane::erase(Event &event)
{
	assert(event.get_address().block < size && event.get_address().valid > PLANE);
	enum status status = data[event.get_address().block]._erase(event);

	/* update values if no errors */
	if(status == 1)
	{
		update_wear_stats();
		free_blocks++;

		/* set next free page if plane was completely full */
		if(next_page.valid < PAGE)
			(void) get_next_page();
	}
	return status;
}

uint Plane::get_size(void) const
{
	return size;
}

const Die &Plane::get_parent(void) const
{
	return parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Plane::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < size)
		return data[address.block].get_last_erase_time();
	else
		return last_erase_time;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
ulong Plane::get_erases_remaining(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PLANE && address.block < size)
		return data[address.block].get_erases_remaining();
	else
		return erases_remaining;
}

/* Block with the most erases remaining is the least worn */
void Plane::update_wear_stats(void)
{
	uint i;
	uint max_index = 0;
	ulong max = data[0].get_erases_remaining();
	for(i = 1; i < size; i++)
		if(data[i].get_erases_remaining() > max)
			max_index = i;
	erases_remaining = max;
	last_erase_time = data[max_index].get_last_erase_time();
	return;
}

enum page_state Plane::get_state(const Address &address) const
{  
	assert(data != NULL && address.block < size && address.valid >= PLANE);
	return data[address.block].get_state(address);
}

enum block_state Plane::get_block_state(const Address &address) const
{
	assert(data != NULL && address.block < size && address.valid >= PLANE);
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

	uint i;
	next_page.valid = PLANE;

	for(i = 0; i < size; i++)
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

/* free_blocks is updated in the write and erase methods */
uint Plane::get_num_free(const Address &address) const
{
	assert(address.valid >= PLANE);
	return free_blocks;
}

uint Plane::get_num_valid(const Address &address) const
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pages_valid();
}

uint Plane::get_num_invalid(const Address & address) const
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pages_invalid();
}

Block *Plane::get_block_pointer(const Address & address)
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pointer();
}

// Inlined for speed
/*Block *Plane::getBlocks() {
	return data;
}*/

