/* run_test2.cpp is part of FlashSim. */

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


#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;


// problem: some of the pointers for the 6 block managers end up in the same LUNs. This is stupid.
// solution: have a method in bm_parent that returns a free block from the LUN with the shortest queue.

//StatisticsGatherer* noise_reads_stats = new StatisticsGatherer();

vector<Thread*> set_experiment(int highest_lba, bool use_flex_reads) {
	BLOCK_MANAGER_ID = 0;
	SCHEDULING_SCHEME = 2;
	GREED_SCALE = 2;
	WEARWOLF_LOCALITY_THRESHOLD = BLOCK_SIZE;
	USE_ERASE_QUEUE = false;

	long log_space_per_thread = highest_lba / 4;

	Thread* initial_write = new Asynchronous_Sequential_Writer(0, log_space_per_thread * 4);

	Thread* exp_reads;
	if (use_flex_reads) {
		exp_reads = new Flexible_Reader_Thread(0, log_space_per_thread * 1, 1000);
	}
	else {
		Simple_Thread* thread_exp_reads = new Synchronous_Sequential_Reader(0, log_space_per_thread * 1);
		thread_exp_reads->set_experiment_thread(true);
		thread_exp_reads->set_num_ios(1000000);
		exp_reads = thread_exp_reads;
	}
	exp_reads->set_experiment_thread(true);

	Simple_Thread* random_writes1 = new Synchronous_Random_Writer(log_space_per_thread * 1 + 1, log_space_per_thread * 2, 472);
	Simple_Thread* random_writes2 = new Synchronous_Random_Writer(log_space_per_thread * 2 + 1, log_space_per_thread * 3, 537);
	Simple_Thread* random_writes3 = new Synchronous_Random_Writer(log_space_per_thread * 3 + 1, log_space_per_thread * 4, 246);

	Simple_Thread* noise_reads1 = new Synchronous_Random_Reader(log_space_per_thread * 1 + 1, log_space_per_thread * 2, 44);
	Simple_Thread* noise_reads2 = new Synchronous_Random_Reader(log_space_per_thread * 2 + 1, log_space_per_thread * 3, 46);
	Simple_Thread* noise_reads3 = new Synchronous_Random_Reader(log_space_per_thread * 3 + 1, log_space_per_thread * 4, 48);

	random_writes1->set_num_ios(INFINITE);
	random_writes2->set_num_ios(INFINITE);
	random_writes3->set_num_ios(INFINITE);
	noise_reads1->set_num_ios(INFINITE);
	noise_reads2->set_num_ios(INFINITE);
	noise_reads3->set_num_ios(INFINITE);

	random_writes1->set_experiment_thread(true);
	random_writes2->set_experiment_thread(true);
	random_writes3->set_experiment_thread(true);

	initial_write->add_follow_up_thread(exp_reads);
	initial_write->add_follow_up_thread(random_writes1);
	initial_write->add_follow_up_thread(random_writes2);
	initial_write->add_follow_up_thread(random_writes3);
	initial_write->add_follow_up_thread(noise_reads1);
	initial_write->add_follow_up_thread(noise_reads2);
	initial_write->add_follow_up_thread(noise_reads3);

	//noise_reads1->set_statistics_gatherer(noise_reads_stats);
	//noise_reads2->set_statistics_gatherer(noise_reads_stats);
	//noise_reads3->set_statistics_gatherer(noise_reads_stats);

	vector<Thread*> threads;
	threads.push_back(initial_write);
	return threads;
}

vector<Thread*> flexible_reads(int highest_lba) {
	return set_experiment(highest_lba, true);
}

vector<Thread*> synch_sequential_reads(int highest_lba) {
	return set_experiment(highest_lba, false);
}

int main()
{
string exp_folder  = "exp_flexible_reads/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 100;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 250000;
	int space_min = 65;
	int space_max = 85;
	int space_inc = 5;

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

	double start_time = Experiment::wall_clock_time();

	vector<Experiment_Result> exp;

	exp.push_back( Experiment::overprovisioning_experiment(flexible_reads,			space_min, space_max, space_inc, exp_folder + "flexible_reads/",			"flexible reads", IO_limit) );
	exp.push_back( Experiment::overprovisioning_experiment(synch_sequential_reads,	space_min, space_max, space_inc, exp_folder + "synch_sequential_reads/",	"synch sequential reads", IO_limit) );


	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write latency, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	//for (int i = 0; i < exp[0].column_names.size(); i++) printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	chdir(exp_folder.c_str());

	Experiment::graph(sx, sy,   "Throughput", 				"throughput", 			24, exp, 30);
	Experiment::graph(sx, sy,   "Write Throughput", 			"throughput_write", 	25, exp, 30);
	Experiment::graph(sx, sy,   "Num Erases", 				"num_erases", 			8, 	exp, 16000);
	Experiment::graph(sx, sy,   "Num Migrations", 			"num_migrations", 		3, 	exp, 500000);

	Experiment::graph(sx, sy,   "Write latency, mean", 			"Write latency, mean", 		9, 	exp, 12000);
	Experiment::graph(sx, sy,   "Write latency, max", 			"Write latency, max", 		14, exp, 40000);
	Experiment::graph(sx, sy,   "Write latency, std", 			"Write latency, std", 		15, exp, 14000);

	Experiment::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 90", exp, 90, 1, 4);
	Experiment::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 80", exp, 80, 1, 4);
	Experiment::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 70", exp, 70, 1, 4);
	Experiment::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 70", exp, 60, 1, 4);

	for (uint i = 0; i < exp.size(); i++) {
		printf("%s\n", exp[i].data_folder.c_str());
		chdir(exp[i].data_folder.c_str());
		Experiment::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, 1, 4);
		Experiment::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, true);
		Experiment::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment::pretty_time(end_time - start_time).c_str());

	return 0;
}

