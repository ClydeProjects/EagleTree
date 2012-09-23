
/* Copyright 2009, 2010 Brendan Tauras */
/* run_test.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Basic test driver
 * Brendan Tauras 2009-11-02
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*> basic_sequential_experiment(int highest_lba, double IO_submission_rate) {
	long max_file_size = highest_lba / 4;
	long num_files = 200;

	Thread* fm1 = new File_Manager(0, highest_lba, num_files, max_file_size, IO_submission_rate, 1, 1);

	vector<Thread*> threads;
	threads.push_back(fm1);
	return threads;
}

vector<Thread*>  sequential_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  sequential_writes_lazy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

/*vector<Thread*>  synch_random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 2000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	double num_threads = SSD_SIZE * PACKAGE_SIZE;
	long space_per_thread = highest_lba / num_threads;

	for (uint i = 0; i < num_threads; i++) {
		long min_lba = space_per_thread * i;
		long max_lba = space_per_thread * (i + 1) - 1;
		t1->add_follow_up_thread(new Synchronous_Random_Thread(min_lba, max_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	}

	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}*/

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 10000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}


vector<Thread*>  random_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  random_writes_lazy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = false;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	string exp_folder  = "exp_copybacks/";
	mkdir(exp_folder.c_str(), 0755);

	bool debug = true;

	load_config();

	/*
	 * sequential_writes_lazy_gc
	 * sequential_writes_greedy_gc
	 * random_writes_greedy_gc
	 * random_writes_lazy_gc
	 */

	if (debug) {
		SSD_SIZE = 4;
		PACKAGE_SIZE = 2;
		DIE_SIZE = 1;
		PLANE_SIZE = 64;
		BLOCK_SIZE = 32;

		PAGE_READ_DELAY = 5;
		PAGE_WRITE_DELAY = 20;
		BUS_CTRL_DELAY = 1;
		BUS_DATA_DELAY = 9;
		BLOCK_ERASE_DELAY = 150;
	} else { // Real size
		SSD_SIZE = 4;
		PACKAGE_SIZE = 2;
		DIE_SIZE = 1;
		PLANE_SIZE = 128;
		BLOCK_SIZE = 32;

		PAGE_READ_DELAY = 5;
		PAGE_WRITE_DELAY = 20;
		BUS_CTRL_DELAY = 1;
		BUS_DATA_DELAY = 8;
		BLOCK_ERASE_DELAY = 150;
	}

	int IO_limit = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 3;
	int used_space = 95;
	int max_copybacks = 4;

	double start_time = Experiment_Runner::wall_clock_time();

	vector<ExperimentResult> exp;
	exp.push_back( Experiment_Runner::copyback_experiment(random_writes_greedy_gc, used_space, max_copybacks, exp_folder + "rand_greed/", "rand greed", IO_limit) );
	exp.push_back( Experiment_Runner::copyback_experiment(random_writes_lazy_gc,   used_space, max_copybacks, exp_folder + "rand_lazy/",  "rand lazy", IO_limit) );

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write wait, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	uint gc_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Copybacks") - exp[0].column_names.begin();
	assert(gc_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = 0; i <= max_copybacks; i += 1)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	chdir(exp_folder.c_str());
	Experiment_Runner::graph(sx, sy,   "Maximum sustainable throughput", "throughput", 24, exp);
	Experiment_Runner::graph(sx, sy,   "# Copyback operations", "copybacks", gc_pos_in_datafile, exp);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	return 0;
}
