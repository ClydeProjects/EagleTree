/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_page.cpp is part of FlashSim. */

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

/* Page class
 * Brendan Tauras 2009-04-06
 *
 * The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */

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

	if (!event.get_noop() && PAGE_ENABLE_DATA)
		global_buffer = (char*)page_data + event.get_address().get_linear_address() * PAGE_SIZE;

	return SUCCESS;
}

enum status Page::_write(Event &event)
{
	event.incr_execution_time(PAGE_WRITE_DELAY);

	if (PAGE_ENABLE_DATA && event.get_payload() != NULL && event.get_noop() == false)
	{
		void *data = (char*)page_data + event.get_address().get_linear_address() * PAGE_SIZE;
		memcpy (data, event.get_payload(), PAGE_SIZE);
	}

	if (event.get_noop() == false)
	{
		if (state != EMPTY) {
			int i = 0;
			i++;
			StateVisualiser::print_page_status();
			event.print();
		}
		assert(state == EMPTY);
		state = VALID;
	}

	return SUCCESS;
}

enum page_state Page::get_state(void) const
{
	return state;
}

void Page::set_state(enum page_state state)
{
	if (this -> state == EMPTY) {
		assert(state != INVALID);
	}
	this -> state = state;
}
