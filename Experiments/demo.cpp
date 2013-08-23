#include "../ssd.h"
using namespace ssd;

// This is a demo experiment. To understand what's going on, go to:
// https://github.com/ClydeProjects/EagleTree/wiki/Tutorial-1---Running-a-simple-experiment

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	Experiment::calibrate_and_save(init, calibration_file);
	delete init;
	Experiment* e = new Experiment();
	e->set_calibration_file(calibration_file);
	Workload_Definition* workload = new Asynch_Random_Workload();
	e->set_workload(workload);
	e->set_io_limit(100000);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
