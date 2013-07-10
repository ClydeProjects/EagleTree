
#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*>  file_manager(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	long num_files = numeric_limits<int>::max();
	int max_file_size = highest_lba / 5;
	Thread* t1 = new File_Manager(0, highest_lba, num_files, max_file_size, IO_submission_rate, 1, 1);
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  two_file_managers(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	long num_files = numeric_limits<int>::max();
	int max_file_size = highest_lba / 5;
	long space_per_thread = highest_lba / 2;
	Thread* t1 = new File_Manager(0, space_per_thread, num_files, max_file_size, IO_submission_rate, 1, 1);
	Thread* t2 = new File_Manager(space_per_thread + 1, space_per_thread * 2, num_files, max_file_size, IO_submission_rate, 1, 1);
	vector<Thread*> threads;
	threads.push_back(t1);
	threads.push_back(t2);
	return threads;
}

vector<Thread*> greedy_eq_prio(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 1;
	SCHEDULING_SCHEME = 2;
	return two_file_managers(highest_lba, IO_submission_rate);
}

vector<Thread*>  greedy_gc_prio(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 1;
	SCHEDULING_SCHEME = 1;
	return two_file_managers(highest_lba, IO_submission_rate);
}

vector<Thread*>  lazy_eq_prio(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 0;
	SCHEDULING_SCHEME = 2;
	return two_file_managers(highest_lba, IO_submission_rate);
}

vector<Thread*> lazy_gc_priorty(int highest_lba, double IO_submission_rate) {
	GREED_SCALE = 0;
	SCHEDULING_SCHEME = 1;
	return two_file_managers(highest_lba, IO_submission_rate);
}



int main()
{
	string exp_folder  = "exp_tuning_for_sequential/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

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

	MAX_SSD_QUEUE_SIZE = 15;

	int IO_limit = 400000;
	int space_min = 93;
	int space_max = 98;
	int space_inc = 1;

	double start_time = Experiment_Runner::wall_clock_time();

	PRINT_LEVEL = 0;

	vector<Experiment_Result> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greedy_eq_prio,	space_min, space_max, space_inc, exp_folder + 		"greedy_eq_prio/", "greedy eq prio", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(greedy_gc_prio,	space_min, space_max, space_inc, exp_folder + 		"greedy_gc_prio/", "greedy gc prio", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(lazy_eq_prio,		space_min, space_max, space_inc, exp_folder + 		"lazy_eq_prio/", "lazy eq prio", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(lazy_gc_priorty,	space_min, space_max, space_inc, exp_folder + 		"lazy_gc_priorty/", "lazy gc priorty", IO_limit) );

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

	Experiment_Runner::graph(sx, sy,   "Throughput", 				"throughput", 				24, exp);
	Experiment_Runner::graph(sx, sy,   "Write Throughput", 			"throughput_write", 		25, exp);
	Experiment_Runner::graph(sx, sy,   "Read Throughput", 			"throughput_read", 			26, exp);
	Experiment_Runner::graph(sx, sy,   "Num Erases", 				"num_erases", 				8, 	exp);
	Experiment_Runner::graph(sx, sy,   "Num Migrations", 			"num_migrations", 			3, 	exp);

	Experiment_Runner::graph(sx, sy,   "Write wait, mean", 			"Write wait, mean", 		9, 	exp);
	Experiment_Runner::graph(sx, sy,   "Write wait, max", 			"Write wait, max", 			14, exp);
	Experiment_Runner::graph(sx, sy,   "Write wait, std", 			"Write wait, std", 			15, exp);

	// VALUES FOR THE TWO LAST PARAMETERS IN cross_experiment_waittime_histogram() and waittime_histogram():
	// 1. Application IOs, Reads+writes
	// 2. Application IOs, Writes
	// 3. Application IOs, Reads
	// 4. Internal operations, All
	// 5. Internal operations, Writes
	// 6. Internal operations, Reads

	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 88", exp, 80, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 95", exp, 70, 1, 4);

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
