#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <sstream>

using namespace ssd;


int main()
{
	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 32;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 100;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 100000;

	//PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 2;
	// DEADLINES?
	//GREED_SCALE = 2;
	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;

	const double used_space = .80; // overprovisioning level for variable random write threads experimentexp_balanced_scheduler
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;

	string exp_folder  = "exp_gc/";
 	mkdir(exp_folder.c_str(), 0755);

	BALANCEING_SCHEME = false;
	Workload_Definition* workload = new Random_Workload(0, logical_space, 10);
	//(Workload_Definition* workload, string data_folder, string name, int IO_limit, int& variable, int min_val, int max_val, int incr) {
	vector<vector<ExperimentResult> > exps;

	int concurrent_gc_min = 5;
	int concurrent_gc_max = 5;

	/*GREED_SCALE = 0;
	exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "Greed0/", "Greed0, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, 1, 6, 1) );
	*/GREED_SCALE = 2;
	exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "Greed1/", "Greed1, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, concurrent_gc_min, concurrent_gc_max, 4) );
	//GREED_SCALE = 2;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "Greed2/", "Greed2, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, 3, 6, 1) );
	delete workload;
	Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = concurrent_gc_min; i <= concurrent_gc_max; i += 1)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);

	double start_time = Experiment_Runner::wall_clock_time();
	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	chdir(".."); // Leaving
	return 0;
}

