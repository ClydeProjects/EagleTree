/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_address.cpp is part of FlashSim. */

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

/* Address class
 * Brendan Tauras 2009-06-19
 *
 * Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking, 
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class.
 */

#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Address::Address():
	package(0),
	die(0),
	plane(0),
	block(0),
	page(0),
	valid(NONE)
{}

// Copy constructors inlined for speed
/*
Address::Address(const Address &address)
{
	*this = address;
	return;
}

Address::Address(const Address *address)
{
	*this = *address;
	return;
}
*/

/* see "enum address_valid" in ssd.h for details on valid status */
Address::Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid):
	package(package),
	die(die),
	plane(plane),
	block(block),
	page(page),
	valid(valid)
{
	return;
}

Address::Address(uint address, enum address_valid valid):
	valid(valid)
{
	set_linear_address(address);
}

/* returns enum indicating to what level two addresses match
 * limits comparison to the fields that are valid */
enum address_valid Address::compare(const Address &address) const
{
	enum address_valid match = NONE;
	if(package == address.package && valid >= PACKAGE && address.valid >= PACKAGE)
	{
		match = PACKAGE;
		if(die == address.die && valid >= DIE && address.valid >= DIE)
		{
			match = DIE;
			if(plane == address.plane && valid >= PLANE && address.valid >= PLANE)
			{
				match = PLANE;
				if(block == address.block && valid >= BLOCK && address.valid >= BLOCK)
				{
					match = BLOCK;
					if(page == address.page && valid >= PAGE && address.valid >= PAGE)
					{
						match = PAGE;
					}
				}
			}
		}
	}
	return match;
}

/* default stream is stdout */
void Address::print(FILE *stream) const
{
	fprintf(stream, "(%d, %d, %d, %d, %d, %d)", package, die, plane, block, page, (int) valid);
	return;
}

void Address::set_linear_address(ulong address)
{
	page = address % BLOCK_SIZE;
	address /= BLOCK_SIZE;
	block = address % PLANE_SIZE;
	address /= PLANE_SIZE;
	plane = address % DIE_SIZE;
	address /= DIE_SIZE;
	die = address % PACKAGE_SIZE;
	address /= PACKAGE_SIZE;
	package = address % SSD_SIZE;
	address /= SSD_SIZE;
}

void Address::set_linear_address(ulong address, enum address_valid valid)
{
	set_linear_address(address);
	this->valid = valid;
}

unsigned long Address::get_linear_address() const
{
	unsigned long 			address = 0;
	if (valid == PAGE) 		address += page;
	if (valid >= BLOCK) 	address += BLOCK_SIZE * block;
	if (valid >= PLANE) 	address += BLOCK_SIZE * PLANE_SIZE * plane;
	if (valid >= DIE) 		address += BLOCK_SIZE * PLANE_SIZE * DIE_SIZE * die;
	if (valid >= PACKAGE) 	address += BLOCK_SIZE * PLANE_SIZE * DIE_SIZE * PACKAGE_SIZE * package;
	return address;
}
