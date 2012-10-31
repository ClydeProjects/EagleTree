#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;


Bus::Bus(Ssd* ssd, uint num_channels, double ctrl_delay, double data_delay, uint table_size, uint max_connections):
	num_channels(num_channels),
	channels((Channel *) malloc(num_channels * sizeof(Channel)))
{
	for(uint i = 0; i < num_channels; i++)
		(void) new (&channels[i]) Channel(ssd, ctrl_delay, data_delay, table_size, max_connections);
}

/* deallocate channels */
Bus::~Bus(void)
{
	for(uint i = 0; i < num_channels; i++)
		channels[i].~Channel();
	free(channels);
	return;
}

enum status Bus::lock(uint channel, double start_time, double duration, Event &event)
{
	return channels[channel].lock(start_time, duration, event);
}
