#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Plane::Plane(long physical_address) : data()
{
	for(uint i = 0; i < PLANE_SIZE; i++)
	{
		int address = physical_address + ( i * BLOCK_SIZE);
		Block b = Block(address);
		data.push_back(b);
	}
}

Plane::Plane() : data() {}

enum status Plane::read(Event &event)
{
	assert(event.get_address().block < PLANE_SIZE && event.get_address().valid > PLANE);
	return data[event.get_address().block].read(event);
}

enum status Plane::write(Event &event)
{
	assert(event.get_address().block < PLANE_SIZE && event.get_address().valid > PLANE);
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

