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

Sequential_Pattern_Detector::Sequential_Pattern_Detector(uint threshold)
: sequential_writes_key_lookup(),
  registration_counter(0),
  listener(NULL),
  sequential_writes_identification_and_data(),
  threshold(threshold) {}

Sequential_Pattern_Detector::~Sequential_Pattern_Detector() {
	delete listener;
}

sequential_writes_tracking const& Sequential_Pattern_Detector::register_event(logical_address lb, double time) {
	sequential_writes_tracking * swt;
	if (sequential_writes_identification_and_data.count(lb) == 1) {
		swt = restart_pattern(lb, time);
	}
	else if (sequential_writes_key_lookup.count(lb) == 1) {
		swt =process_next_write(lb, time);
	}
	else {
		swt = init_pattern(lb, time);
	}

	if (++registration_counter % 50 == 0) {
		remove_old_sequential_writes_metadata(time);
	}
	return *swt;
}

sequential_writes_tracking* Sequential_Pattern_Detector::init_pattern(int key, double time) {
	if (PRINT_LEVEL > 1) {
		printf("init_pattern: %d \n", key);
	}
	sequential_writes_key_lookup[key + 1] = key;
	sequential_writes_tracking* swt = new sequential_writes_tracking(time);
	sequential_writes_identification_and_data[key] = swt;
	return swt;
}

sequential_writes_tracking* Sequential_Pattern_Detector::process_next_write(int lb, double time) {
	logical_address key = sequential_writes_key_lookup[lb];
	sequential_writes_tracking* swm = sequential_writes_identification_and_data[key];
	swm->counter++;
	swm->last_LBA_timestamp = time;
	sequential_writes_key_lookup.erase(lb);
	sequential_writes_key_lookup[lb + 1] = key;
	return swm;
}

sequential_writes_tracking * Sequential_Pattern_Detector::restart_pattern(int key, double time) {
	sequential_writes_tracking* swm = sequential_writes_identification_and_data[key];
	if (swm->counter < threshold) {
		return swm;
	}
	swm->num_times_pattern_has_repeated++;
	assert(sequential_writes_key_lookup.count(key + swm->counter) == 1);
	sequential_writes_key_lookup.erase(key + swm->counter);
	sequential_writes_key_lookup[key] = key + 1;
	swm->counter = 0;
	swm->last_LBA_timestamp = time;
	if (PRINT_LEVEL > 0) {
		printf("SEQUENTIAL PATTERN RESTARTED!  key: %d\n", key);
	}
	return swm;
}

int Sequential_Pattern_Detector::get_current_offset(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		return -1;
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		return swm.counter;
	}
}

int Sequential_Pattern_Detector::get_num_times_pattern_has_repeated(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		return -1;
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		return swm.num_times_pattern_has_repeated;
	}
}

double Sequential_Pattern_Detector::get_arrival_time_of_last_io_in_pattern(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 0) {
		return -1;
	} else {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& swm = *sequential_writes_identification_and_data[key];
		return swm.last_LBA_timestamp;
	}
}

// takes an LBA and, if this LBA belongs to a previusly detected sequential pattern, return the key of the pattern
// the key is the first LBA in the pattern
int Sequential_Pattern_Detector::get_sequential_write_id(logical_address lb) {

	// if order of execution of events matches order of arrival, we should be able to find the id easily
	if (sequential_writes_key_lookup.count(lb) == 1) {
		logical_address key = sequential_writes_key_lookup[lb];
		return key;
	}

	// otherwise, if the order of execution of muffled, we can find it through a range search
	map<logical_address, sequential_writes_tracking*>::iterator iter = sequential_writes_identification_and_data.begin();
	while(iter != sequential_writes_identification_and_data.end())
	{
		long key = (*iter).first;
		sequential_writes_tracking* swt = (*iter).second;
		if (key <= lb && lb <= key + swt->counter - 1) {
			return key;
		}
		iter++;
	}
	return -1;
}

void Sequential_Pattern_Detector::set_listener(Sequential_Pattern_Detector_Listener * new_listener) {
	listener = new_listener;
}

/*void Sequential_Pattern_Detector::remove(logical_address lb) {
	if (sequential_writes_key_lookup.count(lb) == 1) {
		logical_address key = sequential_writes_key_lookup[lb];
		delete sequential_writes_identification_and_data[key];
		sequential_writes_identification_and_data.erase(key);
		sequential_writes_key_lookup.erase(lb);
		sequential_writes_key_lookup.erase(lb + 1);
	}
}*/

// TODO: invoke this method from somewhere
void Sequential_Pattern_Detector::remove_old_sequential_writes_metadata(double time) {
	map<logical_address, sequential_writes_tracking*>::iterator iter = sequential_writes_identification_and_data.begin();
	while(iter != sequential_writes_identification_and_data.end())
	{
		logical_address key = (*iter).first;
		if ((*iter).second->last_LBA_timestamp + 400 < time) {
			if (PRINT_LEVEL > 1) {
				printf("deleting seq write with key %d:\n", key);
			}
			uint next_expected_lba = (*iter).second->counter + key;
			sequential_writes_key_lookup.erase(next_expected_lba);
			delete (*iter).second;;
			sequential_writes_identification_and_data.erase(iter++);
			if (listener != NULL) {
				listener->sequential_event_metadata_removed(key);
			}
		} else {
			++iter;
		}
	}
}

sequential_writes_tracking::sequential_writes_tracking(double time)
	: counter(1),
	  last_LBA_timestamp(time),
	  num_times_pattern_has_repeated(0)
{}

sequential_writes_tracking::~sequential_writes_tracking() {
}
