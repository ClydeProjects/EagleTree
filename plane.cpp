#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Plane::Plane(long physical_address):
	data((Block *) malloc(PLANE_SIZE * sizeof(Block)))
{
	if(data == NULL){
		fprintf(stderr, "Plane error: %s: constructor unable to allocate Block data\n", __func__);
		exit(MEM_ERR);
	}

	for(uint i = 0; i < PLANE_SIZE; i++)
	{
		(void) new (&data[i]) Block(physical_address+(i*BLOCK_SIZE));
	}
}

Plane::~Plane(void)
{
	assert(data != NULL);
	for(uint i = 0; i < PLANE_SIZE; i++)
		data[i].~Block();
	free(data);
}

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

Block *Plane::get_block_pointer(const Address & address)
{
	assert(address.valid >= PLANE);
	return data[address.block].get_pointer();
}

