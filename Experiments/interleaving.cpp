#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main()
{
	string name  = "/exp_interleaving/";
	string exp_folder = get_current_dir_name() + name;
	Experiment::create_base_folder(exp_folder.c_str());

	set_big_SSD();

	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	Experiment::calibrate_and_save(init, calibration_file);

	double writes_probability = 0;
	Workload_Definition* workload = new Asynch_Random_Workload(writes_probability);
	Experiment* e = new Experiment();
	e->set_calibration_file(calibration_file);
	e->set_workload(workload);
	e->set_io_limit(100000);
	e->set_generate_trace_file(true);

	BLOCK_MANAGER_ID = 0;
	ALLOW_DEFERRING_TRANSFERS = false;
	SCHEDULING_SCHEME = 0;
	e->run("no_split");

	ALLOW_DEFERRING_TRANSFERS = true;
	e->run("split");

	delete workload;
	delete init;
	return 0;
}
