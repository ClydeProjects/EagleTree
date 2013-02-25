#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;


Bus::Bus(Ssd* ssd):
	channels((Channel *) malloc(SSD_SIZE * sizeof(Channel)))
{
	for(uint i = 0; i < SSD_SIZE; i++)
		(void) new (&channels[i]) Channel(ssd);
}

/* deallocate channels */
Bus::~Bus(void)
{
	for(uint i = 0; i < SSD_SIZE; i++)
		channels[i].~Channel();
	free(channels);
}

enum status Bus::lock(uint channel, double start_time, double duration, Event &event)
{
	return channels[channel].lock(start_time, duration, event);
}
