/*
 * ssd_sequential_writes_detector.cpp
 *
 *  Created on: Jul 23, 2012
 *      Author: niv
 */

#include "ssd.h"

using namespace ssd;
using namespace std;

#define LIFE_TIME 400 // the number of sequential writes before we recognize the pattern as sequential

Sequential_Pattern_Detector::Sequential_Pattern_Detector()
: registration_counter(0) {}

Sequential_Pattern_Detector::~Sequential_Pattern_Detector() {}

void Sequential_Pattern_Detector::register_event(logical_address lb, double time) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		sequential_writes_key_lookup[lb + 1] = lb;
		sequential_writes_identification_and_data[lb] = new sequential_writes_tracking(time);
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		swm.counter++;
		swm.last_LBA_timestamp = time;
		sequential_writes_key_lookup.erase(lb - 1);
		sequential_writes_key_lookup[lb + 1] = key;
	}
	if (++registration_counter % 1000 == 0) {
		remove_old_sequential_writes_metadata(time);
	}
}

int Sequential_Pattern_Detector::get_counter(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		return -1;
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		return swm.counter;
	}
}

int Sequential_Pattern_Detector::get_sequential_write_id(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		return -1;
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		return key;
	}
}

// TODO: invoke this method from somewhere
void Sequential_Pattern_Detector::remove_old_sequential_writes_metadata(double time) {
	map<logical_address, sequential_writes_tracking*>::iterator iter = sequential_writes_identification_and_data.begin();
	while(iter != sequential_writes_identification_and_data.end())
	{
		if ((*iter).second->last_LBA_timestamp + 400 < time) {
			printf("deleting seq write with key %d:\n", (*iter).first);
			uint next_expected_lba = (*iter).second->counter + (*iter).first;
			sequential_writes_key_lookup.erase(next_expected_lba);
			delete (*iter).second;;
			sequential_writes_identification_and_data.erase(iter++);

		} else {
			++iter;
		}
	}
}

Sequential_Pattern_Detector::sequential_writes_tracking::sequential_writes_tracking(double time)
	: counter(1),
	  last_LBA_timestamp(time)
{}

Sequential_Pattern_Detector::sequential_writes_tracking::~sequential_writes_tracking() {
}
