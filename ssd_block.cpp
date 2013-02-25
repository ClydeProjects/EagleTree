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
	state(FREE),
	erases_remaining(BLOCK_ERASES),
	last_erase_time(0.0),
	erase_before_last_erase_time(0.0),
	modification_time(-1)

{
	if(data == NULL){
		fprintf(stderr, "Block error: %s: constructor unable to allocate Page data\n", __func__);
		exit(MEM_ERR);
	}
	for(uint i = 0; i < BLOCK_SIZE; i++)
		(void) new (&data[i]) Page();
	return;
}

Block::~Block(void)
{
	assert(data != NULL);
	uint i;
	/* call destructor for each Page array element
	 * since we used malloc and placement new */
	for(i = 0; i < BLOCK_SIZE; i++)
		data[i].~Page();
	free(data);
	return;
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

	if(event.get_noop() == false)
	{
		pages_valid++;
		if (pages_valid + pages_invalid < BLOCK_SIZE) {
			state = PARTIALLY_FREE;
		} else {
			state = ACTIVE;
		}
		modification_time = event.get_current_time();

		//Block_manager::instance()->update_block(this);
	}
	return ret;
}

ulong Block::get_age() const {
	return BLOCK_ERASES - erases_remaining;
}

/* updates Event time_taken
 * sets Page statuses to EMPTY
 * updates last_erase_time and erases_remaining
 * returns 1 for success, 0 for failure */
enum status Block::_erase(Event &event)
{
	assert(data != NULL);
	if (!event.get_noop())
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
		}

		event.incr_execution_time(BLOCK_ERASE_DELAY);
		erase_before_last_erase_time = last_erase_time;
		last_erase_time = event.get_current_time();
		erases_remaining--;
		pages_valid = 0;
		pages_invalid = 0;
		state = FREE;
	}
	return SUCCESS;
}

uint Block::get_pages_valid(void) const
{
	return pages_valid;
}

uint Block::get_pages_invalid(void) const
{
	return pages_invalid;
}


enum block_state Block::get_state(void) const
{
	return state;
}

enum page_state Block::get_state(uint page) const
{
	assert(data != NULL && page < BLOCK_SIZE);
	return data[page].get_state();
}

enum page_state Block::get_state(const Address &address) const
{
   assert(data != NULL && address.page < BLOCK_SIZE && address.valid >= BLOCK);
   return data[address.page].get_state();
}

double Block::get_last_erase_time(void) const
{
	return last_erase_time;
}

double Block::get_second_last_erase_time(void) const {
	return erase_before_last_erase_time;
}

ulong Block::get_erases_remaining(void) const
{
	return erases_remaining;
}

void Block::invalidate_page(uint page)
{
	assert(page < BLOCK_SIZE);

	if (data[page].get_state() == INVALID )
		return;

	assert(data[page].get_state() == VALID);

	data[page].set_state(INVALID);

	pages_invalid++;
	pages_valid--;

	//Block_manager::instance()->update_block(this);

	/* update block state */
	if(pages_invalid == BLOCK_SIZE)
		state = INACTIVE;
	else if(pages_valid + pages_invalid == BLOCK_SIZE)
		state = ACTIVE;

	return;
}

double Block::get_modification_time(void) const
{
	return modification_time;
}

/* method to find the next usable (empty) page in this block
 * method is called by write and erase methods and in Plane::get_next_page() */
enum status Block::get_next_page(Address &address) const
{
	for(uint i = 0; i < BLOCK_SIZE; i++)
	{
		if(data[i].get_state() == EMPTY)
		{
			address.set_linear_address(i + physical_address - physical_address % BLOCK_SIZE, PAGE);
			return SUCCESS;
		}
	}
	return FAILURE;
}

long Block::get_physical_address(void) const
{
	return physical_address;
}

Block *Block::get_pointer(void)
{
	return this;
}
