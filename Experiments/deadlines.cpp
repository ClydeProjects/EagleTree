#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	string name  = "/exp_deadlines/";
	string exp_folder = get_current_dir_name() + name;
	Experiment::create_base_folder(exp_folder.c_str());

	set_big_SSD();

	Workload_Definition* init = new Init_Workload();
	string calib_name = "calib.txt";
	Experiment::calibrate_and_save(init, calib_name);

	Experiment* fifo = new Experiment();
	fifo->set_calibration_file(calib_name);
	fifo->set_io_limit(200000);
	SCHEDULING_SCHEME = 0;
	Workload_Definition* workload = new Asynch_Random_Workload();
	fifo->set_workload(workload);
	//fifo->run("fifo");
	delete fifo;

	Experiment* e = new Experiment();
	e->set_calibration_file(calib_name);
	e->set_io_limit(200000);
	int deadline_min = 0;
	int deadline_max = 5000;
	int incr = 500;
	e->set_variable(&WRITE_DEADLINE, deadline_min, deadline_max, incr);
	Workload_Definition* workload = new Asynch_Random_Workload();
	e->set_workload(workload);
	SCHEDULING_SCHEME = 1;
	e->run("deadlines");
	e->draw_graphs();
	delete e;

	//exps.push_back( Experiment_Runner::simple_experiment(workload, init, exp_folder + "no_split/", "no split", IO_limit, OVER_PROVISIONING_FACTOR, op_min, op_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete workload;
	delete init;
	/*Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = deadline_min; i <= deadline_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving*/
	return 0;
}

