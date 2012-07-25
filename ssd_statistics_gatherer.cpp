/*
 * ssd_statistics_gatherer.cpp
 *
 *  Created on: Jul 19, 2012
 *      Author: niv
 */

#include "ssd.h"
using namespace ssd;
#include <stdio.h>

StatisticsGatherer *StatisticsGatherer::inst = NULL;

StatisticsGatherer::StatisticsGatherer(Ssd& ssd)
	: ssd(ssd),
	  sum_bus_wait_time_for_reads_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  num_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  sum_bus_wait_time_for_writes_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  num_writes_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_writes_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0))
{}

StatisticsGatherer::~StatisticsGatherer() {}

void StatisticsGatherer::init(Ssd * ssd)
{
	StatisticsGatherer::inst = new StatisticsGatherer(*ssd);
}

StatisticsGatherer *StatisticsGatherer::get_instance()
{
	return StatisticsGatherer::inst;
}

void StatisticsGatherer::register_completed_event(Event const& event) {
	Address a = event.get_address();
	if (event.get_event_type() == WRITE) {
		sum_bus_wait_time_for_writes_per_LUN[a.package][a.die] += event.get_bus_wait_time();
		num_writes_per_LUN[a.package][a.die]++;
		if (event.is_garbage_collection_op()) {
			num_gc_writes_per_LUN[a.package][a.die]++;
		}
	} else if (event.get_event_type() == READ_COMMAND || event.get_event_type() == READ_TRANSFER) {
		sum_bus_wait_time_for_writes_per_LUN[a.package][a.die] += event.get_bus_wait_time();
		if (event.get_event_type() == READ_TRANSFER) {
			num_reads_per_LUN[a.package][a.die]++;
			if (event.is_garbage_collection_op()) {
				num_gc_reads_per_LUN[a.package][a.die]++;
			}
		}
	}
}

void StatisticsGatherer::print() {
	printf("\n\tnum writes \tnum reads \twrite wait: \tread wait: \tage \n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double average_write_wait_time = sum_bus_wait_time_for_writes_per_LUN[i][j] / num_writes_per_LUN[i][j];
			double average_read_wait_time = sum_bus_wait_time_for_reads_per_LUN[i][j] / num_reads_per_LUN[i][j];
			if (num_writes_per_LUN[i][j] == 0) {
				average_write_wait_time = 0;
			}
			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}
			int avg_age = compute_average_age(i, j);
			printf("P%d D%d\t%d\t\t%d\t\t%f\t\t%f\t\t%d \n", i, j, num_writes_per_LUN[i][j], num_reads_per_LUN[i][j], average_write_wait_time, average_read_wait_time, avg_age);
		}
	}
}

double StatisticsGatherer::compute_average_age(uint package_id, uint die_id) {
	uint sum_age = 0;
	for (uint i = 0; i < DIE_SIZE; i++) {
		for (uint j = 0; j < PLANE_SIZE; j++) {
			 Block& b = ssd.getPackages()[package_id].getDies()[die_id].getPlanes()[i].getBlocks()[j];
			 uint age = BLOCK_ERASES - b.get_erases_remaining();
			 sum_age += age;
		}
	}
	return sum_age / (DIE_SIZE * PLANE_SIZE);
}





