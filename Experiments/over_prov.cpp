#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	string name  = "/exp_over_prov/";
	string exp_folder = get_current_dir_name() + name;
	Experiment::create_base_folder(exp_folder.c_str());

	set_normal_config();

	Workload_Definition* init = new Init_Workload();
	Workload_Definition* workload = new Asynch_Random_Workload();
	Experiment* e = new Experiment();
	e->set_calibration_workload(init);
	e->set_workload(workload);
	e->set_io_limit(20000);
	double* variable = &OVER_PROVISIONING_FACTOR;
	double op_min = 0.60;
	double op_max = 0.70;
	double incr = 0.1;
	e->set_variable(variable, op_min, op_max, incr);

	vector<vector<Experiment_Result> > exps;

	e->run("over_prov");
	e->draw_graphs();

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete workload;
	delete init;

	/*Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = op_min; i <= op_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving*/
	return 0;
}

