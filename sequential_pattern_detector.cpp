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
  threshold(threshold),
  io_num(0) {}

Sequential_Pattern_Detector::~Sequential_Pattern_Detector() {
	map<logical_address, sequential_writes_tracking*>::iterator i = sequential_writes_identification_and_data.begin();
	for (; i != sequential_writes_identification_and_data.end(); i++) {
		sequential_writes_tracking* s = (*i).second;
		if (s != NULL) {
			delete s;
		}
	}
	sequential_writes_identification_and_data.clear();
}

sequential_writes_tracking const& Sequential_Pattern_Detector::register_event(logical_address lb, double time) {
	//printf("lb=%ld  \tt=%f\tA=%d\tB=%d\n", lb, time, sequential_writes_identification_and_data.count(lb), sequential_writes_key_lookup.count(lb));
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
	io_num++;
	return *swt;
}

sequential_writes_tracking* Sequential_Pattern_Detector::init_pattern(int key, double time) {
	if (PRINT_LEVEL > 1) {
		printf("init_pattern: %d \n", key);
	}
	sequential_writes_key_lookup[key + 1] = key;
	sequential_writes_tracking* swt = new sequential_writes_tracking(time, key);
	swt->last_io_num = io_num;
	sequential_writes_identification_and_data[key] = swt;
	return swt;
}

sequential_writes_tracking* Sequential_Pattern_Detector::process_next_write(int lb, double time) {
	int key = sequential_writes_key_lookup[lb];
	sequential_writes_tracking* swm = sequential_writes_identification_and_data[key];
	swm->counter++;
	swm->last_arrival_timestamp = time;
	swm->last_io_num = io_num;
	sequential_writes_key_lookup.erase(lb);
	sequential_writes_key_lookup[lb + 1] = key;
	return swm;
}

sequential_writes_tracking * Sequential_Pattern_Detector::restart_pattern(int key, double time) {
	assert(sequential_writes_identification_and_data.count(key) == 1);
	sequential_writes_tracking* swm = sequential_writes_identification_and_data[key];
	if (swm->counter < threshold) {
		return swm;
	}
	assert(swm->counter != 0);
	swm->num_times_pattern_has_repeated++;
	sequential_writes_key_lookup.erase(key + swm->counter);
	sequential_writes_key_lookup[key + 1] = key;
	swm->counter = 1;
	swm->last_arrival_timestamp = time;
	swm->last_io_num = io_num;
	if (PRINT_LEVEL > 0) {
		printf("SEQUENTIAL PATTERN RESTARTED!  key: %d\n", key);
	}
	return swm;
}

void Sequential_Pattern_Detector::set_listener(Sequential_Pattern_Detector_Listener * new_listener) {
	listener = new_listener;
}

// TODO: invoke this method from somewhere
void Sequential_Pattern_Detector::remove_old_sequential_writes_metadata(double time) {
	map<logical_address, sequential_writes_tracking*>::iterator iter = sequential_writes_identification_and_data.begin();
	while(iter != sequential_writes_identification_and_data.end())
	{
		logical_address key = (*iter).first;
		if ((*iter).second->last_io_num + 200 < io_num) {
			if (PRINT_LEVEL > 1) {
				printf("deleting seq write with key %d:\n", key);
			}
			uint next_expected_lba = (*iter).second->counter + key;
			sequential_writes_key_lookup.erase(next_expected_lba);
			delete (*iter).second;
			sequential_writes_identification_and_data.erase(iter++);
			//sequential_writes_identification_and_data.erase(iter);
			if (listener != NULL) {
				listener->sequential_event_metadata_removed(key, time);
			}
			assert(sequential_writes_identification_and_data.count(key) == 0);
			//++iter;
		} else {
			++iter;
		}
	}
}

sequential_writes_tracking::sequential_writes_tracking(double time, long key)
	: counter(1),
	  num_times_pattern_has_repeated(0),
	  key(key),
	  last_arrival_timestamp(time),
	  init_timestamp(time),
	  last_io_num(0)
{}
