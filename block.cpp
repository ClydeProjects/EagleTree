#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"
#include <sys/types.h>

using namespace ssd;

Block::Block(long physical_address):
			pages_invalid(0),
			physical_address(physical_address),
			data(BLOCK_SIZE, Page()),
			pages_valid(0),
			erases_remaining(BLOCK_ERASES)
{}

Block::Block():
			pages_invalid(0),
			physical_address(0),
			data(BLOCK_SIZE, Page()),
			pages_valid(0),
			erases_remaining(BLOCK_ERASES)
{}

enum status Block::read(Event &event)
{
	return data[event.get_address().page]._read(event);
}

enum status Block::write(Event &event)
{
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
	if(erases_remaining < 1)
	{
		fprintf(stderr, "Block error: %s: No erases remaining when attempting to erase\n", __func__);
		return FAILURE;
	}

	for(uint i = 0; i < BLOCK_SIZE; i++)
	{
		//assert(data[i].get_state() == INVALID);
		data[i].set_state(EMPTY);
		data[i].set_logical_addr(UNDEFINED);
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
