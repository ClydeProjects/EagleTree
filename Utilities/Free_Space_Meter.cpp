#include "../ssd.h"
using namespace ssd;

long Free_Space_Meter::prev_num_free_pages_for_app_writes = 0;
double Free_Space_Meter::timestamp_of_last_change = 0;
double Free_Space_Meter::current_time = 0;
double Free_Space_Meter::total_time_with_free_space = 0;
double Free_Space_Meter::total_time_without_free_space = 0;

void Free_Space_Meter::init() {
	prev_num_free_pages_for_app_writes = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	total_time_with_free_space = 0;
	total_time_without_free_space = 0;
	timestamp_of_last_change = 0;
	current_time = 0;
}

void Free_Space_Meter::register_num_free_pages_for_app_writes(long num_free_pages_for_app_writes, double new_timestamp) {
	if (prev_num_free_pages_for_app_writes == 0 && num_free_pages_for_app_writes > 0) {
		total_time_without_free_space = new_timestamp - timestamp_of_last_change;
		timestamp_of_last_change = new_timestamp;
	} else if (prev_num_free_pages_for_app_writes > 0 && num_free_pages_for_app_writes == 0) {
		total_time_with_free_space = new_timestamp - timestamp_of_last_change;
		timestamp_of_last_change = new_timestamp;
	}
	current_time = new_timestamp;
	prev_num_free_pages_for_app_writes = num_free_pages_for_app_writes;
}

void Free_Space_Meter::print() {

	double diff = current_time - timestamp_of_last_change;
	double time_with_free_space = total_time_with_free_space + ( prev_num_free_pages_for_app_writes > 0 ? diff : 0 );
	double time_without_free_space = total_time_without_free_space + ( prev_num_free_pages_for_app_writes == 0 ? diff : 0 );

	double frac_time_with_free_space = time_with_free_space / (time_with_free_space + time_without_free_space);
	printf("fraction of the time with free space to write on:\t%f\n", frac_time_with_free_space);
}

/*************************************************
/*
/*************************************************/

vector<double> Free_Space_Per_LUN_Meter::total_time_with_free_space;
vector<double> Free_Space_Per_LUN_Meter::total_time_without_free_space;
vector<double> Free_Space_Per_LUN_Meter::timestamp_of_last_change;
vector<bool> Free_Space_Per_LUN_Meter::has_free_pages;

void Free_Space_Per_LUN_Meter::init() {
	total_time_with_free_space = vector<double>(SSD_SIZE * PACKAGE_SIZE, 0);
	total_time_without_free_space = vector<double>(SSD_SIZE * PACKAGE_SIZE, 0);
	timestamp_of_last_change = vector<double>(SSD_SIZE * PACKAGE_SIZE, 0);
	has_free_pages = vector<bool>(SSD_SIZE * PACKAGE_SIZE, true);
}

void Free_Space_Per_LUN_Meter::mark_out_of_space(Address addr, double timestamp) {
	int i = addr.package * PACKAGE_SIZE + addr.die;
	if (has_free_pages[i]) {
		has_free_pages[i] = false;
		double time_diff = timestamp - timestamp_of_last_change[i];
		total_time_with_free_space[i] += time_diff;
		timestamp_of_last_change[i] = timestamp;
	}
}

void Free_Space_Per_LUN_Meter::mark_new_space(Address addr, double timestamp) {
	int i = addr.package * PACKAGE_SIZE + addr.die;
	if (!has_free_pages[i]) {
		has_free_pages[i] = true;
		double time_diff = timestamp - timestamp_of_last_change[i];
		total_time_without_free_space[i] += time_diff;
		timestamp_of_last_change[i] = timestamp;
	}
}

void Free_Space_Per_LUN_Meter::print() {
	printf("Free space on LUNs existed \% of the time\n");
	for (uint i = 0; i < SSD_SIZE * PACKAGE_SIZE; i++) {

		double diff = Free_Space_Meter::get_current_time() - timestamp_of_last_change[i];
		double time_with_free_space = total_time_with_free_space[i] + ( has_free_pages[i] ? diff : 0 );
		double time_without_free_space = total_time_without_free_space[i] + ( !has_free_pages[i] ? diff : 0 );

		double fraction = time_with_free_space / (time_with_free_space + time_without_free_space);
		printf("%d\t%f\n", i, fraction);

	}
}
