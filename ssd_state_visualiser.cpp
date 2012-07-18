/*
 * ssd_visual_tracer.cpp
 *
 *  Created on: Jun 7, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;

Ssd *StateTracer::ssd = NULL;

/*
StateTracer::StateTracer()
{}

StateTracer::~StateTracer() {}
*/

void StateTracer::init(Ssd * ssd)
{
	StateTracer::ssd = ssd;
}

/*
StateTracer *StateTracer::get_instance()
{
	return StateTracer::inst;
}
*/
void StateTracer::print() {
	printf("\n");
	Ssd & ssd_ref = *ssd;
	uint num_valid_pages = 0;
	uint num_invalid_pages = 0;
	uint num_empty_pages = 0;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					for (uint y = 0; y < BLOCK_SIZE; y++) {
						Page const& page = ssd_ref.getPackages()[i].getDies()[j].getPlanes()[k].getBlocks()[t].getPages()[y];
						if (page.get_state() == EMPTY) {
							printf(" ");
							num_empty_pages++;
						} else if (page.get_state() == VALID) {
							printf("V");
							num_valid_pages++;
						} else if (page.get_state() == INVALID) {
							printf("-");
							num_invalid_pages++;
						}
					}
					printf("|");
				}
				printf("\n");
			}
		}
	}
	printf("\n");
	printf("num valid pages: %d\n", num_valid_pages);
	printf("num invalid pages: %d\n", num_invalid_pages);
	printf("num empty pages: %d\n", num_empty_pages);
	printf("\n");
}


