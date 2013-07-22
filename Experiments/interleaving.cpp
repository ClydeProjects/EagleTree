#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

//BOOST_CLASS_EXPORT_GUID(FtlParent, "FtlParent")
//BOOST_CLASS_EXPORT_GUID(FtlImpl_Page, "FtlImpl_Page")

int main()
{
	string name  = "/exp_interleaving/";
	string exp_folder = get_current_dir_name() + name;
	Experiment::create_base_folder(exp_folder.c_str());

	set_normal_config();
	SSD_SIZE = 8;
	PACKAGE_SIZE = 8;
	PLANE_SIZE = 2048;
	BLOCK_SIZE = 256;
	MAX_SSD_QUEUE_SIZE = 64;
	OVER_PROVISIONING_FACTOR = 0.7;


	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	Experiment::calibrate_and_save(init, calibration_file);

	Workload_Definition* workload = new Asynch_Random_Workload();
	Experiment* e = new Experiment();
	e->set_calibration_file(calibration_file);
	e->set_workload(workload);
	e->set_io_limit(50000);

	BLOCK_MANAGER_ID = 0;
	ALLOW_DEFERRING_TRANSFERS = false;
	SCHEDULING_SCHEME = 0;
	e->run("no_split");

	ALLOW_DEFERRING_TRANSFERS = true;
	e->run("split");

	delete workload;
	delete init;

	//SCHEDULING_SCHEME = 1;
	//ALLOW_DEFERRING_TRANSFERS = true;
	//exps.push_back( Experiment_Runner::simple_experiment(workload, init,	exp_folder + "smart/", "smart", IO_limit, WRITE_DEADLINE, op_min, op_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	/*delete init;
	delete workload;
	Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show(0);
	for (double i = op_min; i <= op_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving */
	return 0;
}
