#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <sstream>

using namespace ssd;


int main()
{
	set_normal_config();

	int IO_limit = 100000;
	const double used_space = .80; // overprovisioning level for variable random write threads experimentexp_balanced_scheduler
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;

	string exp_folder  = "exp_gc/";
 	mkdir(exp_folder.c_str(), 0755);


	//Workload_Definition* workload = new Synch_Write(0, 1000);
	//Workload_Definition* workload = new Random_Workload(0, logical_space, 1);
	Workload_Definition* workload = new Asynch_Random_Workload(0, logical_space);
	//(Workload_Definition* workload, string data_folder, string name, int IO_limit, int& variable, int min_val, int max_val, int incr) {
	vector<vector<Experiment_Result> > exps;

	int concurrent_gc_min = 8;
	int concurrent_gc_max = 8;

	/*GREED_SCALE = 0;
	exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "Greed0/", "Greed0, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, 1, 6, 1) );
	*/GREED_SCALE = 2;
	exps.push_back( Experiment::simple_experiment(workload,	exp_folder + "Greed1/", "Greed1, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, concurrent_gc_min, concurrent_gc_max, 4) );
	//GREED_SCALE = 2;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "Greed2/", "Greed2, Max GC 0 - 6", IO_limit, MAX_CONCURRENT_GC_OPS, 3, 6, 1) );
	delete workload;
	Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = concurrent_gc_min; i <= concurrent_gc_max; i += 1)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);

	double start_time = Experiment::wall_clock_time();
	double end_time = Experiment::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment::pretty_time(end_time - start_time).c_str());

	chdir(".."); // Leaving
	return 0;
}

