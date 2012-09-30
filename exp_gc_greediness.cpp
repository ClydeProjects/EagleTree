/*
 * exp_gc_greediness.cpp
 *
 *  Created on: Sep 26, 2012
 *      Author: experiment
 */





#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

// =================================== RANDOM WRITES ==========================================

vector<Thread*>  random_writes_reads_experiment(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	SCHEDULING_SCHEME = 2;
	long num_IOs = numeric_limits<int>::max();
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread_Reader_Writer(0, highest_lba, num_IOs, 624621));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  greed0(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 0;
	return random_writes_reads_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  greed1(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 1;
	return random_writes_reads_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  greed2(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 2;
	return random_writes_reads_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  greed3(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 3;
	return random_writes_reads_experiment(highest_lba, IO_submission_rate);
}

vector<ExperimentResult> random_writes_reads(int space_min, int space_max, int space_inc, string exp_folder, int IO_limit) {
	vector<ExperimentResult> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greed0,	space_min, space_max, space_inc, exp_folder + 		"rand_greed0/", "greed 0", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greed1,	space_min, space_max, space_inc, exp_folder + 		"rand_greed1/", "greed 1", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greed2,	space_min, space_max, space_inc, exp_folder + 		"rand_greed2/", "greed 2", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greed3,	space_min, space_max, space_inc, exp_folder + 		"rand_greed3/", "greed 3", IO_limit) );
	return exp;
}

// =================================== SEQUENTIAL & RANDOM WRITES ==========================================


vector<Thread*> sequential_experiment(int highest_lba, double IO_submission_rate) {
	SCHEDULING_SCHEME = 2;
	USE_ERASE_QUEUE = false;

	long log_space_per_thread = highest_lba / 4;
	long max_file_size = log_space_per_thread / 6;

	Thread* t1 = new Asynchronous_Sequential_Thread(0, log_space_per_thread * 4, 1, WRITE, 100, 0);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, log_space_per_thread * 2, 500000000, 2462, WRITE, IO_submission_rate, 1));
	t1->add_follow_up_thread(new File_Manager(log_space_per_thread * 2 + 1, log_space_per_thread * 3, 1000000, max_file_size, IO_submission_rate, 1, 795));
	t1->add_follow_up_thread(new File_Manager(log_space_per_thread * 3 + 1, log_space_per_thread * 4, 1000000, max_file_size, IO_submission_rate, 2, 46));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}


vector<Thread*> tagging0(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 0;
	return sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> tagging1(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 1;
	return sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> tagging2(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 2;
	return sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> tagging3(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 3;
	return sequential_experiment(highest_lba, IO_submission_rate);
}

vector<ExperimentResult> tagging(int space_min, int space_max, int space_inc, string exp_folder, int IO_limit) {
	vector<ExperimentResult> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(tagging0,	space_min, space_max, space_inc, exp_folder + 		"tag_greed0/", "oracle greed0", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(tagging1,	space_min, space_max, space_inc, exp_folder + 		"tag_greed1/", "oracle greed1", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(tagging2,	space_min, space_max, space_inc, exp_folder + 		"tag_greed2/", "oracle greed2", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(tagging3,	space_min, space_max, space_inc, exp_folder + 		"tag_greed3/", "oracle greed3", IO_limit) );
	return exp;
}

int main()
{
	string exp_folder  = "exp_greediness/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 128;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 90;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 200000;
	int space_min = 60;
	int space_max = 90;
	int space_inc = 5;

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 15;

	double start_time = Experiment_Runner::wall_clock_time();

	vector<ExperimentResult> exp = random_writes_reads(space_min, space_max, space_inc, exp_folder, IO_limit);


	// Print column names for info
	for (uint i = 0; i < exp[0].column_names.size(); i++)
		printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write wait, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	chdir(exp_folder.c_str());

	Experiment_Runner::graph(sx, sy,   "Throughput", 				"throughput", 			24, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Write Throughput", 			"throughput_write", 	25, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Read Throughput", 			"throughput_read", 		26, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Num Erases", 				"num_erases", 			8, 	exp, 16000);
	Experiment_Runner::graph(sx, sy,   "Num Migrations", 			"num_migrations", 		3, 	exp, 500000);

	Experiment_Runner::graph(sx, sy,   "Write wait, mean", 			"Write wait, mean", 	9, 	exp, 12000);
	Experiment_Runner::graph(sx, sy,   "Write wait, max", 			"Write wait, max", 		14, exp, 40000);
	Experiment_Runner::graph(sx, sy,   "Write wait, std", 			"Write wait, std", 		15, exp, 14000);

	Experiment_Runner::graph(sx, sy,   "Read wait, mean", 			"Read wait, mean", 		16,	exp, 2000);
	Experiment_Runner::graph(sx, sy,   "Read wait, max", 			"Read wait, max",		21,	exp, 70000);
	Experiment_Runner::graph(sx, sy,   "Read wait, stdev", 			"Read wait, stdev", 	22,	exp, 4000);

	// VALUES FOR THE TWO LAST PARAMETERS IN cross_experiment_waittime_histogram() and waittime_histogram():
	// 1. Application IOs, Reads+writes
	// 2. Application IOs, Writes
	// 3. Application IOs, Reads
	// 4. Internal operations, All
	// 5. Internal operations, Writes
	// 6. Internal operations, Reads

	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 90", exp, 90, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 80", exp, 80, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 70", exp, 70, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 60", exp, 60, 1, 4);


	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram RW 90", exp, 90, 2, 3);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram RW 80", exp, 80, 2, 3);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram RW 70", exp, 70, 2, 3);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram RW 60", exp, 60, 2, 3);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, 1, 4);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-appIOs", exp[i], used_space_values_to_show, 1);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-rw-appIOs", exp[i], used_space_values_to_show, 2, 3);
		Experiment_Runner::age_histogram			(sx, sy/2, "age_histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	return 0;
}
