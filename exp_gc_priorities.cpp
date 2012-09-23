/*
 * gc_vs_app_priorities_exp.cpp
 *
 *  Created on: Sep 20, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <unistd.h>
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 1000000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  gc_has_priority(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	PRIORITISE_GC = true;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  app_and_gc_have_same_priority(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	PRIORITISE_GC = false;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	string exp_folder  = "exp_gc_priorities/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 1;
	PAGE_WRITE_DELAY = 20;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 9;
	BLOCK_ERASE_DELAY = 150;

	int IO_limit = 100000;
	int space_min = 70;
	int space_max = 85;
	int space_inc = 5;

	double start_time = Experiment_Runner::wall_clock_time();

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 15;

	vector<ExperimentResult> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(app_and_gc_have_same_priority, space_min, space_max, space_inc, "rand_greed/", "rand greed", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(gc_has_priority, space_min, space_max, space_inc, "rand_lazy/", "rand lazy", IO_limit) );

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write wait, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	//	for (int i = 0; i < exp[0].column_names.size(); i++) printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	chdir(exp_folder.c_str());
	Experiment_Runner::graph(sx, sy,   "Throughput for diff GC priorities", "throughput", 24, exp);

	Experiment_Runner::graph(sx, sy,   "Total number of erases", "num erases", 8, exp);

	for (uint i = 0; i < exp.size(); i++) {
		printf("%s\n", exp[i].data_folder.c_str());
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());


}
