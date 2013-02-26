#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Package::Package(long physical_address):
	data((Die *) malloc(PACKAGE_SIZE * sizeof(Die)))
{
	if(data == NULL){
		fprintf(stderr, "Package error: %s: constructor unable to allocate Die data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < PACKAGE_SIZE; i++)
		(void) new (&data[i]) Die(physical_address+(DIE_SIZE*PLANE_SIZE*BLOCK_SIZE*i));
}

Package::~Package()
{
	assert(data != NULL);
	for(uint i = 0; i < PACKAGE_SIZE; i++)
		data[i].~Die();
	free(data);
}

enum status Package::read(Event &event)
{
	assert(data != NULL && event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].read(event);
}

enum status Package::write(Event &event)
{
	assert(data != NULL && event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].write(event);
}

enum status Package::erase(Event &event)
{
	assert(data != NULL && event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	enum status status = data[event.get_address().die].erase(event);
	return status;
}

Block *Package::get_block_pointer(const Address & address)
{
	assert(address.valid >= DIE);
	return data[address.die].get_block_pointer(address);
}
