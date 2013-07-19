#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	set_normal_config();

	string exp_folder  = "over_prov/";
 	mkdir(exp_folder.c_str(), 0755);

 	Workload_Definition* init = new Init_Workload();
	Workload_Definition* workload = new Asynch_Random_Workload();
	int IO_limit = 200000;
	double op_min = 0.20;
	double op_max = 0.80;
	double incr = 0.1;
	vector<vector<Experiment_Result> > exps;
	exps.push_back( Experiment::simple_experiment(workload, init,	exp_folder + "over_prov/", "over prov", IO_limit, OVER_PROVISIONING_FACTOR, op_min, op_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete workload;
	Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = op_min; i <= op_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving
	return 0;
}

