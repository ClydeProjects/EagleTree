
#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	long num_IOs = numeric_limits<int>::max();
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  naive_lazy(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = false;
	SCHEDULING_SCHEME = 0;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  naive_greedy(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = true;
	SCHEDULING_SCHEME = 0;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  intelligent_lazy(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = false;
	SCHEDULING_SCHEME = 1;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  intelligent_greedy(int highest_lba, double IO_submission_rate) {
	GREEDY_GC = true;
	SCHEDULING_SCHEME = 1;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	string exp_folder  = "exp_scheduling/";
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

	double start_time = Experiment_Runner::wall_clock_time();

	vector<ExperimentResult> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(naive_lazy,				space_min, space_max, space_inc, exp_folder + "naive_lazy/", "naive_lazy", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(naive_greedy,				space_min, space_max, space_inc, exp_folder + "naive_greedy/", "naive_greedy", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(intelligent_lazy,			space_min, space_max, space_inc, exp_folder + "intelligent_lazy/", "intelligent_lazy", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(intelligent_greedy,		space_min, space_max, space_inc, exp_folder + "intelligent_greedy/", "intelligent_greedy", IO_limit) );

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

	Experiment_Runner::graph(sx, sy,   "Maximum sustainable throughput", 	"throughput", 			24, 	exp);
	Experiment_Runner::graph(sx, sy,   "Num Erases", 						"num_erases", 			8, 		exp);
	Experiment_Runner::graph(sx, sy,   "Latency std", 						"latency std", 			15, 	exp);
	Experiment_Runner::graph(sx, sy,   "Num Migrations", 					"num_migrations", 		3, 		exp);
	Experiment_Runner::graph(sx, sy,   "Write wait, max", 					"Write wait, max (µs)", 14, 	exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q25", "Write wait, Q25 (µs)", 11, exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q50", "Write wait, Q50 (µs)", 12, exp);
	//Experiment_Runner::graph(sx, sy,   "Write wait, Q75", "Write wait, Q75 (µs)", 13, exp);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, true);
		Experiment_Runner::age_histogram			(sx, sy/2, "age_histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	return 0;
}
