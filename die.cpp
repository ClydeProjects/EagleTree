#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Die::Die(long physical_address):
	data((Plane *) malloc(DIE_SIZE * sizeof(Plane))),
	currently_executing_io_finish_time(0.0),
	last_read_io(UNDEFINED)
{
	if(data == NULL){
		fprintf(stderr, "Die error: %s: constructor unable to allocate Plane data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < DIE_SIZE; i++)
		(void) new (&data[i]) Plane(physical_address+(PLANE_SIZE*BLOCK_SIZE*i));
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

enum status Die::erase(Event &event)
{
	assert(data != NULL);
	assert(event.get_address().plane < DIE_SIZE && event.get_address().valid > DIE);
	assert(currently_executing_io_finish_time <= event.get_current_time());

	enum status status = data[event.get_address().plane].erase(event);
	currently_executing_io_finish_time = event.get_current_time();
	return status;
}

double Die::get_currently_executing_io_finish_time() {
	return currently_executing_io_finish_time;
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
