#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"
#include <sys/types.h>

using namespace ssd;

Block::Block(long physical_address):
			pages_invalid(0),
			physical_address(physical_address),
			data((Page *) malloc(BLOCK_SIZE * sizeof(Page))),
			pages_valid(0),
			erases_remaining(BLOCK_ERASES)
{
	if(data == NULL){
		fprintf(stderr, "Block error: %s: constructor unable to allocate Page data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < BLOCK_SIZE; i++)
		(void) new (&data[i]) Page();
}

Block::~Block()
{
	assert(data != NULL);
	for(uint i = 0; i < BLOCK_SIZE; i++)
		data[i].~Page();
	free(data);
}

enum status Block::read(Event &event)
{
	assert(data != NULL);
	return data[event.get_address().page]._read(event);
}

enum status Block::write(Event &event)
{
	assert(data != NULL);
	if (event.get_address().page > 0 && data[event.get_address().page - 1].get_state() == EMPTY) {
		printf("\n");
		event.print();
		assert(data[event.get_address().page - 1].get_state() != EMPTY);
	}
	enum status ret = data[event.get_address().page]._write(event);
	pages_valid++;
	return ret;
}

/* updates Event time_taken
 * sets Page statuses to EMPTY
 * updates last_erase_time and erases_remaining
 * returns 1 for success, 0 for failure */
enum status Block::_erase(Event &event)
{
	assert(data != NULL);

	if(erases_remaining < 1)
	{
		fprintf(stderr, "Block error: %s: No erases remaining when attempting to erase\n", __func__);
		return FAILURE;
	}

	for(uint i = 0; i < BLOCK_SIZE; i++)
	{
		//assert(data[i].get_state() == INVALID);
		data[i].set_state(EMPTY);
	}

	event.incr_execution_time(BLOCK_ERASE_DELAY);
	erases_remaining--;
	pages_valid = 0;
	pages_invalid = 0;
	return SUCCESS;
}

void Block::invalidate_page(uint page)
{
	assert(page < BLOCK_SIZE);
	data[page].set_state(INVALID);
	pages_invalid++;
	pages_valid--;
}
