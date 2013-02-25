/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_package.cpp is part of FlashSim. */

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

/* Package class
 * Brendan Tauras 2009-11-03
 *
 * The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Package::Package(Channel &channel, uint package_size, long physical_address):
	size(package_size),
	data((Die *) malloc(package_size * sizeof(Die))),
	least_worn(0),
	last_erase_time(0.0)
{
	if(data == NULL){
		fprintf(stderr, "Package error: %s: constructor unable to allocate Die data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < size; i++)
		(void) new (&data[i]) Die(*this, channel, physical_address+(DIE_SIZE*PLANE_SIZE*BLOCK_SIZE*i));
}

Package::~Package(void)
{
	assert(data != NULL);
	for(uint i = 0; i < size; i++)
		data[i].~Die();
	free(data);
}

enum status Package::read(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].read(event);
}

enum status Package::write(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].write(event);
}

enum status Package::erase(Event &event)
{
	assert(data != NULL && event.get_address().die < size && event.get_address().valid > PACKAGE);
	enum status status = data[event.get_address().die].erase(event);
	return status;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
double Package::get_last_erase_time(const Address &address) const
{
	assert(data != NULL);
	if(address.valid > PACKAGE && address.die < size)
		return data[address.die].get_last_erase_time(address);
	else
		return last_erase_time;
}

enum page_state Package::get_state(const Address &address) const
{
	assert(data != NULL && address.die < size && address.valid >= PACKAGE);
	return data[address.die].get_state(address);
}

enum block_state Package::get_block_state(const Address &address) const
{
	assert(data != NULL && address.die < size && address.valid >= PACKAGE);
	return data[address.die].get_block_state(address);
}

Block *Package::get_block_pointer(const Address & address)
{
	assert(address.valid >= DIE);
	return data[address.die].get_block_pointer(address);
}
