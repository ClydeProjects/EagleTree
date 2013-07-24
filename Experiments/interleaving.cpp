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

	set_big_SSD();

	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	Experiment::calibrate_and_save(init, calibration_file);

	Workload_Definition* workload = new Asynch_Random_Workload();
	Experiment* e = new Experiment();
	e->set_calibration_file(calibration_file);
	e->set_workload(workload);
	e->set_io_limit(100000);

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
