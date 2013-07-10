#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

//BOOST_CLASS_EXPORT_GUID(FtlParent, "FtlParent")
//BOOST_CLASS_EXPORT_GUID(FtlImpl_Page, "FtlImpl_Page")

int main()
{
	set_normal_config();
	string exp_folder  = "interleaving_exp/";
 	mkdir(exp_folder.c_str(), 0755);

	Workload_Definition* workload = new Asynch_Random_Workload();
	int IO_limit = 200000;
	long op_min = 1000000;
	long op_max = 1000000;
	long incr = 1;
	vector<vector<Experiment_Result> > exps;

	Workload_Definition* init = new Init_Workload();
	string a = "/" + exp_folder;
	string calibrated_file = get_current_dir_name() + a + "interleaving_calibrated_state.txt";
	Experiment_Runner::calibrate_and_save(calibrated_file, init);

	SCHEDULING_SCHEME = 0;
	ALLOW_DEFERRING_TRANSFERS = false;
	exps.push_back( Experiment_Runner::simple_experiment(workload, exp_folder + "no_split/", "no split", IO_limit, WRITE_DEADLINE, op_min, op_max, incr, calibrated_file) );

	SCHEDULING_SCHEME = 0;
	ALLOW_DEFERRING_TRANSFERS = true;
	exps.push_back( Experiment_Runner::simple_experiment(workload, exp_folder + "split/", "split", IO_limit, WRITE_DEADLINE, op_min, op_max, incr, calibrated_file) );

	SCHEDULING_SCHEME = 1;
	ALLOW_DEFERRING_TRANSFERS = true;
	//exps.push_back( Experiment_Runner::simple_experiment(workload, init,	exp_folder + "smart/", "smart", IO_limit, WRITE_DEADLINE, op_min, op_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete init;
	delete workload;
	Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show(0);
	for (double i = op_min; i <= op_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving
	return 0;
}
