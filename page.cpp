#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>

#include "ssd.h"

namespace ssd {
	/*
	 * Buffer used for accessing data pages.
	 */
	void *global_buffer;

}

using namespace ssd;

enum status Page::_read(Event &event)
{
	event.incr_execution_time(PAGE_READ_DELAY);

	/*if (PAGE_ENABLE_DATA)
		global_buffer = (char*)page_data + event.get_address().get_linear_address() * PAGE_SIZE;
*/
	return SUCCESS;
}

enum status Page::_write(Event &event)
{
	event.incr_execution_time(PAGE_WRITE_DELAY);
	/*if (PAGE_ENABLE_DATA && event.get_payload() != NULL && event.get_noop() == false)
	{
		void *data = (char*)page_data + event.get_address().get_linear_address() * PAGE_SIZE;
		memcpy (data, event.get_payload(), PAGE_SIZE);
	}*/
	if (state != EMPTY) {
		printf("You are trying to overwrite a page that is not free. This is illegal. The operations is: \n");
		event.print();
	}
	logical_addr = event.get_logical_address();
	assert(state == EMPTY);
	state = VALID;
	return SUCCESS;
}
