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
