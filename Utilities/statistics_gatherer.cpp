/*
 * ssd_statistics_gatherer.cpp
 *
 *  Created on: Jul 19, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;
#include <stdio.h>
#include <sstream>
#include <algorithm>

StatisticsGatherer *StatisticsGatherer::inst = NULL;

const double StatisticsGatherer::wait_time_histogram_bin_size = 500;
const double StatisticsGatherer::io_counter_window_size = 200000; // second
bool StatisticsGatherer::record_statistics = true;

StatisticsGatherer::StatisticsGatherer()
	: num_gc_cancelled_no_candidate(0),
	  num_gc_cancelled_not_enough_free_space(0),
	  num_gc_cancelled_gc_already_happening(0),
	  bus_wait_time_for_reads_per_LUN(SSD_SIZE, vector<vector<double> >(PACKAGE_SIZE, vector<double>())),
	  num_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_mapping_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_mapping_writes_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  bus_wait_time_for_writes_per_LUN(SSD_SIZE, vector<vector<double> >(PACKAGE_SIZE, vector<double>())),
	  num_writes_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_writes_per_LUN_origin(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_writes_per_LUN_destination(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_scheduled_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  queue_length_tracker(0),
	  num_executed_gc_ops(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_live_pages_in_gc_exec(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  sum_gc_wait_time_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  gc_wait_time_per_LUN(SSD_SIZE, vector<vector<double> >(PACKAGE_SIZE, vector<double>())),
	  num_copy_backs_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_erases_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_executed(0),
	  num_migrations(0),
	  num_gc_scheduled(0),
	  num_gc_targeting_package_die_class(0),
	  num_gc_targeting_package_die(0),
	  num_gc_targeting_package_class(0),
	  num_gc_targeting_package(0),
	  num_gc_targeting_class(0),
	  num_gc_targeting_anything(0),
	  num_wl_writes_per_LUN_origin(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_wl_writes_per_LUN_destination(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  end_time(0)
{}

vector<vector<double> > num_valid_pages_per_gc_op;
vector<vector<int> > num_executed_gc_ops;

StatisticsGatherer::~StatisticsGatherer() {

}

void StatisticsGatherer::init()
{
	if (inst != NULL) delete inst;
	inst = new StatisticsGatherer();
}

StatisticsGatherer *StatisticsGatherer::get_global_instance()
{
	return inst;
}

void StatisticsGatherer::register_completed_event(Event const& event) {
	if (!record_statistics) {
		return;
	}
	end_time = max(end_time, event.get_current_time());

	uint current_window = floor(event.get_current_time() / io_counter_window_size);
	while (application_io_history.size() < current_window + 1) application_io_history.push_back(0);
	while (non_application_io_history.size() < current_window + 1)non_application_io_history.push_back(0);

	if (event.get_event_type() != READ_COMMAND && event.get_event_type() != TRIM) {
		if (event.is_original_application_io()) application_io_history[current_window]++;
		else non_application_io_history[current_window]++;
	}

	Address a = event.get_address();
	if (event.get_event_type() == WRITE || event.get_event_type() == COPY_BACK) {
		if (event.is_original_application_io()) {
			num_writes_per_LUN[a.package][a.die]++;
			bus_wait_time_for_writes_per_LUN[a.package][a.die].push_back(event.get_latency());

			/*StatisticData::register_statistic("all_writes", {
					new Integer(event.get_latency())
			});

			StatisticData::register_field_names("all_writes", {
					"write_latency"
			});*/

		}
		else if (event.is_wear_leveling_op()) {
			Address replace_add = event.get_replace_address();
			num_wl_writes_per_LUN_origin[replace_add.package][replace_add.die]++;
			num_wl_writes_per_LUN_destination[a.package][a.die]++;
		}
		else if (event.is_garbage_collection_op()) {
			Address replace_add = event.get_replace_address();
			num_gc_writes_per_LUN_origin[replace_add.package][replace_add.die]++;
			num_gc_writes_per_LUN_destination[a.package][a.die]++;

			sum_gc_wait_time_per_LUN[a.package][a.die] += event.get_latency();
			gc_wait_time_per_LUN[a.package][a.die].push_back(event.get_latency());

		}
		else if (event.is_mapping_op()) {
			num_mapping_writes_per_LUN[a.package][a.die]++;
		}
	} else if (event.get_event_type() == READ_TRANSFER) {
		if (event.is_original_application_io()) {
			bus_wait_time_for_reads_per_LUN[a.package][a.die].push_back(event.get_latency());
			num_reads_per_LUN[a.package][a.die]++;


			/*StatisticData::register_statistic("all_reads", {
					new Integer(event.get_latency())
			});

			StatisticData::register_field_names("all_reads", {
					"read_latency"
			});*/

		} else if (event.is_garbage_collection_op()) {
			num_gc_reads_per_LUN[a.package][a.die]++;
		} else if (event.is_mapping_op()) {
			num_mapping_reads_per_LUN[a.package][a.die]++;
		}
	} else if (event.get_event_type() == ERASE) {
		num_erases_per_LUN[a.package][a.die]++;
	}
	if (event.get_event_type() == COPY_BACK) {
		num_copy_backs_per_LUN[a.package][a.die]++;
	}

	double bucket = ceil(max(0.0, event.get_latency() - wait_time_histogram_bin_size / 2) / wait_time_histogram_bin_size)*wait_time_histogram_bin_size;
	if      (event.is_original_application_io() && event.get_event_type() == WRITE) { wait_time_histogram_appIOs_write[bucket]++; wait_time_histogram_appIOs_write_and_read[bucket]++; }
	else if (event.is_original_application_io() && event.get_event_type() == READ_TRANSFER)  { wait_time_histogram_appIOs_read[bucket]++; wait_time_histogram_appIOs_write_and_read[bucket]++; }
	else if (!event.is_original_application_io() && event.get_event_type() == WRITE) { wait_time_histogram_non_appIOs_write[bucket]++;  }
	else if (!event.is_original_application_io() && event.get_event_type() == READ_TRANSFER) { wait_time_histogram_non_appIOs_read[bucket]++; }
	if      (!event.is_original_application_io()) { wait_time_histogram_non_appIOs_all[bucket]++; }

	if      (event.is_original_application_io() && event.get_event_type() == WRITE) { latency_history_write.push_back(event.get_latency()); latency_history_write_and_read.push_back(event.get_latency()); }
	else if (event.is_original_application_io() && event.get_event_type() == READ_TRANSFER) { latency_history_read.push_back(event.get_latency()); latency_history_write_and_read.push_back(event.get_latency()); }

}



void StatisticsGatherer::register_scheduled_gc(Event const& gc) {
	if (!record_statistics) {
		return;
	}
	if (inst != this) inst->register_scheduled_gc(gc); // Do the same for global instance
	num_gc_scheduled++;

	int age_class = gc.get_age_class();
	Address addr = gc.get_address();

	if (addr.valid == DIE && age_class != UNDEFINED) {
		num_gc_targeting_package_die_class++;
		num_gc_scheduled_per_LUN[addr.package][addr.die]++;
	}
	else if (addr.valid == PACKAGE && age_class != UNDEFINED) {
		num_gc_targeting_package_class++;
	}
	else if (addr.valid == NONE && age_class != UNDEFINED) {
		num_gc_targeting_class++;
	}
	else if (addr.valid == DIE) {
		num_gc_targeting_package_die++;
		num_gc_scheduled_per_LUN[addr.package][addr.die]++;
	}
	else if (addr.valid == PACKAGE) {
		num_gc_targeting_package++;
	}
	else {
		num_gc_targeting_anything++;
	}
}

void StatisticsGatherer::register_executed_gc(Block const& victim) {
	if (!record_statistics) {
		return;
	}
	if (inst != this) inst->register_executed_gc(victim); // Do the same for global instance
	num_gc_executed++;
	num_migrations += victim.get_pages_valid();
	Address a = Address(victim.get_physical_address(), BLOCK);
	//num_valid_pages_per_gc_op[gc.get_address().package][gc.get_address().die] += victim.get_pages_valid();
	num_executed_gc_ops[a.package][a.die] += 1;
	num_live_pages_in_gc_exec[a.package][a.die] += victim.get_pages_valid();
}

void StatisticsGatherer::register_events_queue_length(uint queue_size, double time) {
	if (!record_statistics) {
		return;
	}
	if (inst != this) inst->register_events_queue_length(queue_size, time); // Do the same for global instance
	if (time == 0) return;
	uint current_window = floor(time / queue_length_tracker_resolution);
	while (queue_length_tracker.size() > 0 && queue_length_tracker.size() < current_window) {
		queue_length_tracker.push_back(queue_length_tracker.back());
//		printf("-> COPIED LAST (vs=%d, window=%d)", queue_length_tracker.size(), current_window);
	}
	if (queue_length_tracker.size() == current_window) {
		//printf("queue length %d\n", queue_size);
		queue_length_tracker.push_back(queue_size);
//		printf("Q: %f\t: %d\t (%d)", time, queue_size, queue_length_tracker.size() * queue_length_tracker_resolution);
//		printf("-> SAMPLED");
//		printf("\n");
	}
}

template <class T>
double get_max(vector<T> const& vector)
{
	double max = 0;
	int index = UNDEFINED;
	for (uint i = 0; i < vector.size(); i++) {
		if (vector[i] > max) {
			max = vector[i];
			index = i;
		}
	}
    return max;
}

template <class T>
double get_sum(vector<T> const& vector)
{
	double sum = 0;
	for (uint i = 0; i < vector.size(); i++) {
		sum += vector[i];
	}
    return sum;
}

template <class T>
double get_average(vector<T> const& vector)
{
	double sum = get_sum(vector);
	if (sum == 0) return 0;
    double avg = sum / vector.size();
    return avg;
}

template <class T>
double get_std(vector<T> const& vector)
{
	if (vector.size() == 0) return 0;
	double avg = get_average(vector);
	double std = 0;
	for (uint i = 0; i < vector.size(); i++) {
		double diff = (vector[i] - avg);
		diff *= diff;
		std += diff;
	}
	std /= vector.size();
	std = sqrt (std);
    return std;
}

template <class T>
double get_sum(vector<vector<T> > const& vector)
{
	double sum = 0;
	for (uint i = 0; i < vector.size(); i++) {
		sum += get_sum(vector[i]);
	}
    return sum;
}

template <class T>
double get_average(vector<vector<T> > const& vector)
{
	double sum = get_sum(vector);
	if (sum == 0) return 0;
    double avg = sum / (vector.size() * vector[0].size());
    return avg;
}

template <class T>
double get_std(vector<vector<T> > const& vector)
{
	double avg = get_average(vector);
	if (avg == 0) return 0;
	double std = 0;
	for (uint i = 0; i < vector.size(); i++) {
		for (uint j = 0; j < vector[i].size(); j++) {
			double diff = (vector[i][j] - avg);
			diff *= diff;
			std += diff;
		}
	}
	std /= vector.size() * vector[0].size();
	std = sqrt (std);
    return std;
}


template <class T>
void flatten(vector<vector<vector<T> > > const& vec, vector<T>& outcome)
{
	for (uint i = 0; i < vec.size(); i++) {
		for (uint j = 0; j < vec[i].size(); j++) {
			outcome.insert(outcome.end(), vec[i][j].begin(), vec[i][j].end());
		}
	}
}


void StatisticsGatherer::print() const {
	printf("\n\t");
	printf("num writes\t");
	printf("num reads\t");
	//printf("WL writes\t");
	printf("GC writes\t");
	printf("GC reads\t");
	printf("copy backs\t");
	printf("erases\t\t");
	printf("avg write wait\t");
	printf("avg read wait\t");
	printf("\n");

	double avg_overall_write_wait_time = 0;
	double avg_overall_read_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {

			double avg_write_wait_time = get_average(bus_wait_time_for_writes_per_LUN[i][j]);
			avg_overall_write_wait_time += avg_write_wait_time;

			double average_read_wait_time = get_average(bus_wait_time_for_reads_per_LUN[i][j]);
			avg_overall_read_wait_time += average_read_wait_time;

			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}

			printf("C%d D%d\t", i, j);

			printf("%d\t\t", num_writes_per_LUN[i][j]);
			printf("%d\t\t", num_reads_per_LUN[i][j]);

			//printf("%d\t\t", num_wl_writes_per_LUN_origin[i][j]);
			printf("%d\t\t", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d\t\t", num_gc_reads_per_LUN[i][j]);
			printf("%d\t\t", num_copy_backs_per_LUN[i][j]);


			printf("%d\t\t", num_erases_per_LUN[i][j]);

			printf("%f\t", avg_write_wait_time);
			printf("%f\t", average_read_wait_time);

			//printf("%f\t", avg_age);
			printf("\n");
		}
	}
	avg_overall_write_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	avg_overall_read_wait_time /= SSD_SIZE * PACKAGE_SIZE;

	vector<double> all_write_latency;
	flatten(bus_wait_time_for_writes_per_LUN, all_write_latency);
	double write_std = get_std(all_write_latency);
	double max_write = get_max(all_write_latency);

	vector<double> all_read_latency;
	flatten(bus_wait_time_for_reads_per_LUN, all_read_latency);
	double read_std = get_std(all_read_latency);
	double max_read = get_max(all_read_latency);

	printf("\nTotals:\t");

	printf("%d\t\t", (int) get_sum(num_writes_per_LUN));
	printf("%d\t\t", (int) get_sum(num_reads_per_LUN));
	//printf("%d\t\t", (int) get_sum(num_wl_writes_per_LUN_origin));
	printf("%d\t\t", (int) get_sum(num_gc_writes_per_LUN_origin));
	printf("%d\t\t", (int) get_sum(num_gc_reads_per_LUN));
	printf("%d\t\t", (int) get_sum(num_copy_backs_per_LUN));
	printf("%d\t\t", (int) get_sum(num_erases_per_LUN));
	printf("%d\t\t", (int) avg_overall_write_wait_time);

	printf("%d\t\t", (int)avg_overall_read_wait_time);
	printf("\n");

	printf("\t\t\t\t\t\t\t\t\t\t\t\t\t%d", (int)write_std);
	printf("\t\t%d\t\t", (int)read_std);
	printf("\n");

	printf("\t\t\t\t\t\t\t\t\t\t\t\t\t%d", (int)max_write);
	printf("\t\t%d\t\t", (int)max_read);

	printf("\n");
	//printf("Erase avg:\t%f \n", get_average(num_erases_per_LUN));
	//printf("Erase std:\t%f \n", get_std(num_erases_per_LUN));
	double milliseconds = end_time / 1000;
	printf("Time taken (ms):\t%d\n", (int)milliseconds);

	printf("Thoughput (IO/sec)\n");
	printf("read throughput:\t%d\n", (int)get_reads_throughput());
	printf("writes throughput:\t%d\n", (int)get_writes_throughput());
	printf("total throughput:\t%d\n", (int)get_reads_throughput() + (int)get_writes_throughput());

	double queue_length = get_average(queue_length_tracker);
	printf("avg queue length: %f\n", queue_length);
	printf("\n\n");
}

void StatisticsGatherer::print_mapping_info() {
	int all_mapping_reads = get_sum(num_mapping_reads_per_LUN);
	if (all_mapping_reads == 0) {
		return;
	}

	printf("\n\t");
	printf("map writes\t");
	printf("map reads\t");
	printf("\n");

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			printf("C%d D%d\t", i, j);
			printf("%d\t\t", num_mapping_writes_per_LUN[i][j]);
			printf("%d\t\t", num_mapping_reads_per_LUN[i][j]);
			printf("\n");
		}
	}

	printf("\nTotals:\t");

	printf("%d\t\t", (int) get_sum(num_mapping_writes_per_LUN));
	printf("%d\t\t", (int) all_mapping_reads);
	printf("\n\n");
}

void StatisticsGatherer::print_simple(FILE* stream) {

	vector<double> all_write_latency;
	flatten(bus_wait_time_for_writes_per_LUN, all_write_latency);
	double write_avg = get_average(all_write_latency);
	double write_std = get_std(all_write_latency);
	double write_max = get_max(all_write_latency);

	fprintf(stream, "num writes:\t%d\n", total_writes());
	fprintf(stream, "avg writes latency:\t%f\n", write_avg);
	fprintf(stream, "std writes latency:\t%f\n", write_std);
	fprintf(stream, "max writes latency:\t%f\n\n", write_max);

	vector<double> all_reads_latency;
	flatten(bus_wait_time_for_reads_per_LUN, all_reads_latency);
	double reads_avg = get_average(all_reads_latency);
	double reads_std = get_std(all_reads_latency);
	double reads_max = get_max(all_reads_latency);

	fprintf(stream, "num reads:\t%d\n", total_reads());
	fprintf(stream, "avg reads latency:\t%f\n", reads_avg);
	fprintf(stream, "std reads latency:\t%f\n", reads_std);
	fprintf(stream, "max reads latency:\t%f\n\n", reads_max);

	fprintf(stream, "num gc reads:\t%d\n", (int)get_sum(num_gc_reads_per_LUN));
	fprintf(stream, "num gc writes:\t%d\n", (int)get_sum(num_gc_writes_per_LUN_destination));
	fprintf(stream, "num erases:\t%d\n\n", (int)get_sum(num_erases_per_LUN));

	int read_throughput = (int) get_reads_throughput();
	int writes_throughput = (int) get_writes_throughput();

	fprintf(stream, "Thoughput (IO/sec)\n");
	fprintf(stream, "total throughput:\t%d\n", read_throughput + writes_throughput);
	fprintf(stream, "read throughput:\t%d\n", read_throughput);
	fprintf(stream, "writes throughput:\t%d\n", writes_throughput);

}

void StatisticsGatherer::print_gc_info() {
	printf("\n\t");
	printf("GC writes from\t");
	printf("GC writes to\t");
	printf("GC reads\t");
	printf("GC scheduled\t");
	printf("GC exec\t\t");
	printf("GC wait \t\t");
	printf("copy backs\t");
	printf("erases\t\t");
	printf("\n");

	double avg_overall_gc_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {

			double average_gc_wait_per_LUN = sum_gc_wait_time_per_LUN[i][j] / num_gc_writes_per_LUN_destination[i][j];

			double average_migrations_per_gc = (double) num_live_pages_in_gc_exec[i][j] / num_executed_gc_ops[i][j];

			avg_overall_gc_wait_time += average_gc_wait_per_LUN;

			printf("C%d D%d\t", i, j);

			printf("%d\t\t", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d\t\t", num_gc_writes_per_LUN_destination[i][j]);
			printf("%d\t\t", num_gc_reads_per_LUN[i][j]);
			printf("%d\t\t", num_gc_scheduled_per_LUN[i][j]);
			printf("%d\t\t", num_executed_gc_ops[i][j]);
			printf("%f\t\t", average_gc_wait_per_LUN);
			printf("%d\t\t", num_copy_backs_per_LUN[i][j]);
			printf("%d\t\t", num_erases_per_LUN[i][j]);
			printf("%f\t\t", average_migrations_per_gc);
			printf("\n");
		}
	}

	avg_overall_gc_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	printf("\nTotals:\t");
	printf("%d\t\t", (int) get_sum(num_gc_writes_per_LUN_origin));
	printf("%d\t\t", (int) get_sum(num_gc_writes_per_LUN_destination));
	printf("%d\t\t", (int) get_sum(num_gc_reads_per_LUN));
	printf("%d\t\t", (int) get_sum(num_gc_scheduled_per_LUN));
	printf("%d\t\t", (int) get_sum(num_executed_gc_ops));
	printf("%f\t\t", avg_overall_gc_wait_time);
	printf("%d\t\t", (int) get_sum(num_copy_backs_per_LUN));
	printf("%d\t\t", (int) get_sum(num_erases_per_LUN));
	printf("\n\n");
	printf("num scheduled gc: %ld \n", num_gc_scheduled);
	printf("num executed gc: %ld \n", num_gc_executed);
	printf("num migrations per gc: %f \n", (double)num_migrations / num_gc_executed);
	printf("standard dev num migrations per gc: %f \n", StatisticData::get_standard_deviation("GC_eff_with_writes", 1));
	printf("\n");
	printf("gc targeting package die class: %ld \n", num_gc_targeting_package_die_class);
	printf("gc targeting package die: %ld \n", num_gc_targeting_package_die);
	printf("gc targeting package class: %ld \n", num_gc_targeting_package_class);
	printf("gc targeting package: %ld \n", num_gc_targeting_package);
	printf("gc targeting class: %ld \n", num_gc_targeting_class);
	printf("gc targeting anything: %ld \n", num_gc_targeting_anything);
	printf("\n");
	printf("num_gc_cancelled_no_candidate: %ld \n", num_gc_cancelled_no_candidate);
	printf("num_gc_cancelled_not_enough_free_space: %ld \n", num_gc_cancelled_not_enough_free_space);
	printf("num_gc_cancelled_gc_already_happening: %ld \n", num_gc_cancelled_gc_already_happening);
}



string StatisticsGatherer::totals_csv_header() {
	stringstream ss;
	string q = "\"";
	string qc = "\", ";
	vector<string> names = totals_vector_header();
	for (int i = 0; i < names.size(); i++) {
		ss << q << names[i] << q;
		if (i != names.size()-1) ss << ", ";
	}
	return ss.str();
}

vector<string> StatisticsGatherer::totals_vector_header() {
	vector<string> result;
	// 1 "Used space %"
	result.push_back("Num Writes");
	result.push_back("Num Reads");
	result.push_back("GC Write");
	result.push_back("GC Reads");
	result.push_back("GC Scheduled");
	result.push_back("GC Wait");
	result.push_back("Copybacks");
	result.push_back("Erases");

	result.push_back("Write latency, mean (us)"); // 10
	result.push_back("Write latency, min (us)");
	result.push_back("Write latency, Q25 (us)");
	result.push_back("Write latency, Q50 (us)");
	result.push_back("Write latency, Q75 (us)");
	result.push_back("Write latency, max (us)");
	result.push_back("Write latency, stdev (us)");

	result.push_back("Read latency, mean (us)");
	result.push_back("Read latency, min (us)");
	result.push_back("Read latency, Q25 (us)"); // 20
	result.push_back("Read latency, Q50 (us)");
	result.push_back("Read latency, Q75 (us)");
	result.push_back("Read latency, max (us)");
	result.push_back("Read latency, stdev (us)");

	result.push_back("GC latency, stdev (us)"); // 25
	result.push_back("Channel Utilization (%)");

	result.push_back("GC Efficiency");

	//result.push_back("max write wait (us)"); // 15
	//result.push_back("max read wait (us)");
	//result.push_back("max GC wait (us)");
	// Sustainable throughput (us) 18
	return result;
}

uint StatisticsGatherer::total_reads() const {
	return get_sum(num_reads_per_LUN);
}

uint StatisticsGatherer::total_writes() const {
	return get_sum(num_writes_per_LUN);
}

double StatisticsGatherer::get_reads_throughput() const {
	return (total_reads() / end_time) * 1000 * 1000;
}

double StatisticsGatherer::get_writes_throughput() const {
	return (total_writes() / end_time) * 1000 * 1000;
}

double StatisticsGatherer::get_total_throughput() const {
	return get_reads_throughput() + get_writes_throughput();
}

string StatisticsGatherer::totals_csv_line() {
	uint total_writes = get_sum(num_writes_per_LUN);
	uint total_reads = get_sum(num_reads_per_LUN);
	uint total_gc_writes = get_sum(num_gc_writes_per_LUN_origin);
	uint total_gc_reads = get_sum(num_gc_reads_per_LUN);
	uint total_gc_scheduled = get_sum(num_gc_scheduled_per_LUN);
	uint total_copy_backs = get_sum(num_copy_backs_per_LUN);
	uint total_erases = get_sum(num_erases_per_LUN);

	double avg_overall_gc_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double average_gc_wait_per_LUN = sum_gc_wait_time_per_LUN[i][j] / num_gc_writes_per_LUN_origin[i][j];
			avg_overall_gc_wait_time += average_gc_wait_per_LUN;
		}
	}
	avg_overall_gc_wait_time /= SSD_SIZE * PACKAGE_SIZE;

	// Compute standard deviation of read, write and GC wait

	double stddev_overall_gc_wait_time = 0;
	uint gc_wait_time_population = 0;

	vector<double> all_write_wait_times;
	vector<double> all_read_wait_times;

	flatten(bus_wait_time_for_writes_per_LUN, all_write_wait_times);
	flatten(bus_wait_time_for_reads_per_LUN, all_read_wait_times);

	sort(all_write_wait_times.begin(), all_write_wait_times.end());
	sort(all_read_wait_times.begin(), all_read_wait_times.end());

	if (all_write_wait_times.size() == 0) all_write_wait_times.push_back(-1);
	if (all_read_wait_times.size() == 0) all_read_wait_times.push_back(-1);

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < gc_wait_time_per_LUN[i][j].size(); k++) {
				stddev_overall_gc_wait_time += pow(gc_wait_time_per_LUN[i][j][k] - avg_overall_gc_wait_time, 2);
			}
			gc_wait_time_population += gc_wait_time_per_LUN[i][j].size();
		}
	}
	stddev_overall_gc_wait_time = sqrt(stddev_overall_gc_wait_time / gc_wait_time_population);

	stringstream ss;
	ss << total_writes << ", ";
	ss << total_reads << ", ";
	ss << total_gc_writes << ", ";
	ss << total_gc_reads << ", ";
	ss << total_gc_scheduled << ", ";
	ss << avg_overall_gc_wait_time << ", ";
	ss << total_copy_backs << ", ";
	ss << total_erases << ", ";

	ss << get_average(all_write_wait_times) << ", ";  // mean
	ss << all_write_wait_times.front() << ", "; // min
	ss << all_write_wait_times[all_write_wait_times.size() * .25] << ", "; // Q25
	ss << all_write_wait_times[all_write_wait_times.size() * .5]  << ", "; // Q50
	ss << all_write_wait_times[all_write_wait_times.size() * .75] << ", "; // Q75
	ss << all_write_wait_times.back() << ", ";  // max
	ss << get_std(all_write_wait_times) << ", ";

	//printf("write max:  %f\n", all_write_wait_times.back());
	//printf("write std:  %f\n", get_std(all_write_wait_times));

	ss << get_average(all_read_wait_times) << ", ";  // mean
	ss << all_read_wait_times.front() << ", "; // min
	ss << all_read_wait_times[all_read_wait_times.size() * .25] << ", "; // Q25
	ss << all_read_wait_times[all_read_wait_times.size() * .5]  << ", "; // Q50
	ss << all_read_wait_times[all_read_wait_times.size() * .75] << ", "; // Q75
	ss << all_read_wait_times.back() << ", ";  // max
	ss << get_std(all_read_wait_times) << ", ";

	//printf("read max:  %f\n", all_read_wait_times.back());
	//printf("read std:  %f\n", get_std(all_read_wait_times));

	ss << stddev_overall_gc_wait_time << ", ";

	ss << Utilization_Meter::get_avg_channel_utilization() << ", ";

	ss << (double)num_migrations / num_gc_executed;

	return ss.str();
}

string StatisticsGatherer::latency_csv() {
	stringstream ss;

	vector<vector<double> > latency_histories;
	vector<string> latency_names;
	latency_names.push_back("Write latency (µs)");
	latency_histories.push_back(latency_history_write);
	latency_names.push_back("Read latency (µs)");
	latency_histories.push_back(latency_history_read);
	latency_names.push_back("Write+Read latency (µs)");
	latency_histories.push_back(latency_history_write_and_read);

	ss << "\"IO #\"";
	for (uint i = 0; i < latency_names.size(); i++)
		ss << ", " << "\"" << latency_names[i] << "\"";
	ss << "\n";

	uint max_vector_size = 0;
	for (uint i = 0; i < latency_histories.size(); i++)
		max_vector_size = max(max_vector_size, (uint) latency_histories[i].size());

	for (uint pos = 0; pos < max_vector_size; pos++) {
		ss << pos;
		for (uint i = 0; i < latency_histories.size(); i++) {
			ss << ", " << (pos < latency_histories[i].size() ? latency_histories[i][pos] : std::numeric_limits<double>::signaling_NaN());
		}
		ss << "\n";
	}

	return ss.str();
}

string StatisticsFormatter::histogram_csv(map<double, uint> histogram) {
	stringstream ss;
	ss << "\"Interval\", \"Frequency\"" << "\n";
	for (map<double,uint>::iterator it = histogram.begin(); it != histogram.end(); ++it) {
		ss << it->first << ", " << it->second << "\n";
	}
	return ss.str();
}

string StatisticsFormatter::stacked_histogram_csv(vector<map<double, uint> > histograms, vector<string> names) {
	stringstream ss;
	ss << "\"Interval\"";
	for (uint i = 0; i < names.size(); i++) ss << ", \"" << names[i] << "\"";
	ss << "\n";
	vector< map<double,uint>::iterator > its(histograms.size());
	for (uint i = 0; i < histograms.size(); i++) {
		map<double,uint>& hist = histograms[i];
		map<double,uint>::iterator iter = hist.begin();
		its[i] = iter;
	}
	double minimum = numeric_limits<double>::max();
	bool the_end = false;
	while (!the_end) {
		minimum = numeric_limits<double>::max();
		for (uint i = 0; i < its.size(); i++) {
			if (its[i] != histograms[i].end()) {
				map<double,uint>::iterator iter = its[i];
				double d = iter->first;
				minimum = min(minimum, d);
			}
		}
		if (minimum == numeric_limits<double>::max()) the_end = true;
		else {
			ss << minimum;
			for (uint i = 0; i < its.size(); i++) {
				ss << ", " << histograms[i][minimum];
				if (its[i] != histograms[i].end() && its[i]->first == minimum) {
					its[i]++;
				}
			}
			ss << "\n";
		}
	}

	return ss.str();
}

vector<double> StatisticsGatherer::max_waittimes() {
	vector<double> result;
	result.push_back(wait_time_histogram_appIOs_write_and_read.size() > 0 ? wait_time_histogram_appIOs_write_and_read.rbegin()->first : 0);
	result.push_back(wait_time_histogram_appIOs_write.size()          > 0 ? wait_time_histogram_appIOs_write.rbegin()->first          : 0);
	result.push_back(wait_time_histogram_appIOs_read.size()           > 0 ? wait_time_histogram_appIOs_read.rbegin()->first           : 0);
	result.push_back(wait_time_histogram_non_appIOs_all.size()        > 0 ? wait_time_histogram_non_appIOs_all.rbegin()->first        : 0);
	result.push_back(wait_time_histogram_non_appIOs_write.size()      > 0 ? wait_time_histogram_non_appIOs_write.rbegin()->first      : 0);
	result.push_back(wait_time_histogram_non_appIOs_read.size()       > 0 ? wait_time_histogram_non_appIOs_read.rbegin()->first       : 0);
	return result;
}

string StatisticsGatherer::wait_time_histogram_appIOs_csv() {
	return StatisticsFormatter::histogram_csv(wait_time_histogram_appIOs_write_and_read);
}

string StatisticsGatherer::wait_time_histogram_all_IOs_csv() {
	vector<map<double, uint> > histograms(0);
	vector<string> names(0);
	names.push_back("Application IOs, Reads+writes");
	histograms.push_back(wait_time_histogram_appIOs_write_and_read);
	names.push_back("Application IOs, Writes");
	histograms.push_back(wait_time_histogram_appIOs_write);
	names.push_back("Application IOs, Reads");
	histograms.push_back(wait_time_histogram_appIOs_read);
	names.push_back("Internal operations, All");
	histograms.push_back(wait_time_histogram_non_appIOs_all);
	names.push_back("Internal operations, Writes");
	histograms.push_back(wait_time_histogram_non_appIOs_write);
	names.push_back("Internal operations, Reads");
	histograms.push_back(wait_time_histogram_non_appIOs_read);

	/*assert(wait_time_histogram_appIOs_write_and_read.size() > 0);
	assert(wait_time_histogram_appIOs_write.size() > 0);
	assert(wait_time_histogram_appIOs_read.size() > 0);
	assert(wait_time_histogram_non_appIOs_all.size() > 0);
	assert(wait_time_histogram_non_appIOs_write.size() > 0);
	assert(wait_time_histogram_non_appIOs_read.size() > 0);*/

	return StatisticsFormatter::stacked_histogram_csv(histograms, names);
}

string StatisticsGatherer::queue_length_csv() {
	stringstream ss;
	ss << "\"Time (µs)\", \"Queued events\"" << "\n";
	for (uint window = 0; window < queue_length_tracker.size(); window++) {
		ss << window*queue_length_tracker_resolution << ", " << queue_length_tracker[window] << "\n";
	}
	return ss.str();
}

string StatisticsGatherer::app_and_gc_throughput_csv() {
	stringstream ss;
	ss << "\"Time (µs)\", \"Application IOs/s\", \"Internal operations/s\"" << "\n";
	for (uint i = 0; i < application_io_history.size(); i++) {
		double second = 1000000;
		double app_throughput = second / io_counter_window_size * application_io_history[i];
		double non_app_throughput = second / io_counter_window_size * non_application_io_history[i];

		//double app_throughput = (double) application_io_history[i]/io_counter_window_size * 1000;
		//double non_app_throughput = (double) non_application_io_history[i]/io_counter_window_size*1000;
		ss << io_counter_window_size * i << ", " << app_throughput << ", " << non_app_throughput << "\n";
	}
	return ss.str();
}
void StatisticsGatherer::print_csv() {
	printf("\n");
	printf("Channel,");
	printf("Die,");
	printf("num writes,");
	printf("num reads,");
	printf("GC writes,");
	printf("GC reads,");
	printf("GC scheduled,");
	printf("copy backs,");
	printf("erases,");
	printf("write wait,");
	printf("read wait,");
	//printf("age\t");

	printf("\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double average_write_wait_time = get_average(bus_wait_time_for_writes_per_LUN[i][j]);
			double average_read_wait_time = get_average(bus_wait_time_for_reads_per_LUN[i][j]);
			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}
			printf("%d,%d,", i, j);

			printf("%d,", num_writes_per_LUN[i][j]);
			printf("%d,", num_reads_per_LUN[i][j]);

			printf("%d,", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d,", num_gc_reads_per_LUN[i][j]);
			printf("%d,", num_gc_scheduled_per_LUN[i][j]);
			printf("%d,", num_copy_backs_per_LUN[i][j]);
			printf("%d,", num_erases_per_LUN[i][j]);

			printf("%f,", average_write_wait_time);
			printf("%f,", average_read_wait_time);

			//printf("%f\t", avg_age);
			printf("\n");
		}
	}
}

const double SsdStatisticsExtractor::age_histogram_bin_size = 1;
SsdStatisticsExtractor *SsdStatisticsExtractor::inst = NULL;

SsdStatisticsExtractor::SsdStatisticsExtractor(Ssd& ssd)
	: ssd(ssd)
{}

SsdStatisticsExtractor::~SsdStatisticsExtractor()
{}

void SsdStatisticsExtractor::init(Ssd * ssd) {
	if (inst != NULL) delete inst;
	inst = new SsdStatisticsExtractor(*ssd);
}

uint SsdStatisticsExtractor::max_age() {
	uint max_age = 0;
	map<double, uint> age_histogram;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* block = get_instance()->ssd.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					uint age = BLOCK_ERASES - block->get_erases_remaining();
					max_age = max(age, max_age);
				}
			}
		}
	}
	return max_age;
}

uint SsdStatisticsExtractor::max_age_freq() {
	uint max_age_freq = 0;
	map<double, uint> age_histogram;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* block = get_instance()->ssd.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					uint age = BLOCK_ERASES - block->get_erases_remaining();
					age_histogram[floor((double) age / age_histogram_bin_size)*age_histogram_bin_size]++;
				}
			}
		}
	}

	for (map<double,uint>::iterator it = age_histogram.begin(); it != age_histogram.end(); ++it) {
		max_age_freq = max(it->second, max_age_freq);
	}

	return max_age_freq;
}

string SsdStatisticsExtractor::age_histogram_csv() {
	map<double, uint> age_histogram;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block* block = get_instance()->ssd.get_package(i)->get_die(j)->get_plane(k)->get_block(t);
					uint age = BLOCK_ERASES - block->get_erases_remaining();
					age_histogram[floor((double) age / age_histogram_bin_size)*age_histogram_bin_size]++;
				}
			}
		}
	}
	return StatisticsFormatter::histogram_csv(age_histogram);
}

SsdStatisticsExtractor* SsdStatisticsExtractor::get_instance() {
	return inst;
}



