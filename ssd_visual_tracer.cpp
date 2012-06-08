/*
 * ssd_visual_tracer.cpp
 *
 *  Created on: Jun 7, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;

VisualTracer *VisualTracer::inst = NULL;

VisualTracer::VisualTracer()
  : trace(SSD_SIZE, std::vector<std::vector<char> >(PACKAGE_SIZE, std::vector<char>(0) ))
{}

VisualTracer::~VisualTracer() {}

void VisualTracer::init()
{
	VisualTracer::inst = new VisualTracer();
}

VisualTracer *VisualTracer::get_instance()
{
	return VisualTracer::inst;
}

void VisualTracer::register_completed_event(Event const& event) {
	Address add = event.get_address();

	int i = event.get_start_time() + event.get_bus_wait_time() - trace[add.package][add.die].size();
	write(add.package, add.die, ' ', i);

	if (event.get_event_type() == WRITE) {
		write(add.package, add.die, 't', 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY);
		write(add.package, add.die, 'w', PAGE_WRITE_DELAY - 1);
	} else if (event.get_event_type() == READ_COMMAND) {
		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		write(add.package, add.die, 'r', PAGE_READ_DELAY - 1);
	} else if (event.get_event_type() == READ_TRANSFER) {
		write(add.package, add.die, 't', BUS_CTRL_DELAY + BUS_DATA_DELAY - 1);
	} else if (event.get_event_type() == ERASE) {
		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		write(add.package, add.die, 'e', BLOCK_ERASE_DELAY - 1);
	}
	trace[add.package][add.die].push_back('|');
}


void VisualTracer::write(int package, int die, char symbol, int length) {
	for (int i = 0; i < length; i++) {
		trace[package][die].push_back(symbol);
	}
}

void VisualTracer::print() {
	printf("\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			printf("p%d d%d :", i, j);
			for (uint c = 0; c < trace[i][j].size(); c++) {
				printf("%c", trace[i][j][c]);
			}
			printf("\n");
		}
	}
}

