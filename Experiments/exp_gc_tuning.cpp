
#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*> basic_sequential_experiment(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 2;

	long max_file_size = highest_lba / 4;
	long num_files = 100000;

	Thread* fm1 = new File_Manager(0, highest_lba, num_files, max_file_size, IO_submission_rate, 1, 1);

	vector<Thread*> threads;
	threads.push_back(fm1);
	return threads;
}

vector<Thread*>  sequential_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = true;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  sequential_writes_lazy_gc(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = false;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	long num_IOs = numeric_limits<int>::max();
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  greedy_gc_priority(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = true;
	SCHEDULING_SCHEME = 1;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  greedy_equal_priority(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = true;
	SCHEDULING_SCHEME = 2;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  lazy_gc_priority(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = false;
	SCHEDULING_SCHEME = 1;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  lazy_equal_priority(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = false;
	SCHEDULING_SCHEME = 2;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	string exp_folder  = "exp_gc_tuning/";
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

	int IO_limit = 200000;
	int space_min = 75;
	int space_max = 90;
	int space_inc = 5;

	double start_time = Experiment::wall_clock_time();

	vector<Experiment_Result> exp;
	exp.push_back( Experiment::overprovisioning_experiment(greedy_gc_priority,		space_min, space_max, space_inc, exp_folder + "greedy_gc_priority/", "greedy, gc prio", IO_limit) );
	exp.push_back( Experiment::overprovisioning_experiment(greedy_equal_priority,	space_min, space_max, space_inc, exp_folder + "greedy_equal_priority/", "greedy, equal prio", IO_limit) );
	exp.push_back( Experiment::overprovisioning_experiment(lazy_gc_priority,			space_min, space_max, space_inc, exp_folder + "lazy_gc_priority/", "lazy, gc prio", IO_limit) );
	exp.push_back( Experiment::overprovisioning_experiment(lazy_equal_priority,		space_min, space_max, space_inc, exp_folder + "lazy_equal_priority/", "lazy, equal prio", IO_limit) );

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

	Experiment::graph(sx, sy,   "Maximum sustainable throughput", 	"throughput", 			24, 	exp);
	Experiment::graph(sx, sy,   "Num Erases", 						"num_erases", 			8, 		exp);
	Experiment::graph(sx, sy,   "Latency std", 						"latency std", 			15, 	exp);
	Experiment::graph(sx, sy,   "Num Migrations", 					"num_migrations", 		3, 		exp);
	Experiment::graph(sx, sy,   "Write wait, max", 					"Write wait, max (µs)", 14, 	exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q25", "Write wait, Q25 (µs)", 11, exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q50", "Write wait, Q50 (µs)", 12, exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q75", "Write wait, Q75 (µs)", 13, exp);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, true);
		Experiment::age_histogram			(sx, sy/2, "age_histograms", exp[i], used_space_values_to_show);
		Experiment::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment::pretty_time(end_time - start_time).c_str());

	return 0;
}
