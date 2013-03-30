#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	set_normal_config();

	GREED_SCALE = 2;

	int IO_limit = 200000;
	const double used_space = .70; // overprovisioning level for variable random write threads experimentexp_balanced_scheduler
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;

	string exp_folder  = "2deadlines_results/";
 	mkdir(exp_folder.c_str(), 0755);

	Workload_Definition* workload = new Asynch_Random_Workload(0, logical_space);
	vector<vector<ExperimentResult> > exps;

	int deadline_min = 600;
	int deadline_max = 600;
	int incr = 200;

	SCHEDULING_SCHEME = 0;
	exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "smart_sched/", "smart sched", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

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

