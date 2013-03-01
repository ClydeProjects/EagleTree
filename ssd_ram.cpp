#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;
 
Ram::Ram() {
	assert(RAM_READ_DELAY >= 0);
	assert(RAM_WRITE_DELAY >= 0);
}

Ram::~Ram() {}

enum status Ram::read(Event &event)
{
	event.incr_execution_time(RAM_READ_DELAY);
	return SUCCESS;
}

enum status Ram::write(Event &event)
{
	event.incr_execution_time(RAM_WRITE_DELAY);
	return SUCCESS;
}
