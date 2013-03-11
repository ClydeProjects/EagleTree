#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	set_normal_config();

	int IO_limit = 10000;
	const double used_space = .80; // overprovisioning level for variable random write threads experimentexp_balanced_scheduler
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;

	string exp_folder  = "deadlines_results/";
 	mkdir(exp_folder.c_str(), 0755);

	Workload_Definition* workload = new Asynch_Random_Workload(0, logical_space);
	vector<vector<ExperimentResult> > exps;

	int deadline_min = 0;
	int deadline_max = 0;
	int incr = 200;

	exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines/", "deadlines", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete workload;
	Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = deadline_min; i <= deadline_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving
	return 0;
}

