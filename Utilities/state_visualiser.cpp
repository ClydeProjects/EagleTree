/*
 * ssd_state_visualiser.cpp
 *
 *  Created on: Jun 7, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

Ssd *StateVisualiser::ssd = NULL;

void StateVisualiser::init(Ssd * ssd)
{
	StateVisualiser::ssd = ssd;
}

void StateVisualiser::print_page_status() {
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
						Page const& page = ssd_ref.get_package(i)->get_die(j)->get_plane(k)->get_block(t)->get_page(y);
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
		printf("\n");
	}
	printf("\n");
	printf("num valid pages: %d\n", num_valid_pages);
	printf("num invalid pages: %d\n", num_invalid_pages);
	printf("num empty pages: %d\n", num_empty_pages);
	printf("num pages total: %d\n", num_empty_pages + num_invalid_pages + num_valid_pages);
	printf("\n");
}

void StateVisualiser::print_block_ages() {
	printf("\n");
	Ssd & ssd_ref = *ssd;
	uint block_count = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	uint total_age = 0;
	uint oldest_age = std::numeric_limits<uint>::min();;
	uint youngest_age = std::numeric_limits<uint>::max();;
	double standard_age_deviation = 0;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* block = ssd_ref.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					uint age = block->get_age();
					printf("% 7d|", age);
					total_age += age;
					oldest_age = max(oldest_age, age);
					youngest_age = min(youngest_age, age);
				}
				printf("\n");
			}
		}
		printf("\n");
	}
	printf("\n");

	double average_age = (double) total_age / block_count;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* block = ssd_ref.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					uint age = BLOCK_ERASES - block->get_erases_remaining();
					standard_age_deviation += pow(age - average_age, 2);
				}
			}
		}
	}
	standard_age_deviation = sqrt(standard_age_deviation / block_count);
	printf("min block age: %d\n", youngest_age);
	printf("max block age: %d\n", oldest_age);
	printf("average block age: %.1f\n", average_age);
	printf("standard deviation: %.1f\n", standard_age_deviation);
	printf("total age of all blocks: %d\n", total_age);
	printf("\n");
}

void StateVisualiser::print_page_valid_histogram() {
	printf("\n");
	Ssd & ssd_ref = *ssd;
	vector<int> histogram = vector<int>(BLOCK_SIZE, 0);
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* b = ssd_ref.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					histogram[b->get_pages_valid()]++;
					if (b->get_pages_valid() == 0) {
						Address a = Address();
						a.set_linear_address(b->get_physical_address());
						a.print();
						printf("   block:  %d  valid: %d   invalid: %d\n", b->get_physical_address(), b->get_pages_valid(), b->get_pages_invalid());
					}
 				}
			}
		}
	}
	printf("histogram:\n");
	for (int i = 0; i < BLOCK_SIZE; i++) {
		printf("\t%d\t%d\n", i, histogram[i]);
	}
	printf("\n");
}
