#include "../ssd.h"
using namespace ssd;

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	SCHEDULING_SCHEME = 1; // use the noop IO scheduler during calibration because it's fastest in terms of real execution time
	Experiment::calibrate_and_save(init, calibration_file);
	delete init;
	Experiment* e = new Experiment();
	e->set_calibration_file(calibration_file);
	Workload_Definition* workload = new Asynch_Random_Workload();
	e->set_workload(workload);
	e->set_io_limit(1000000);
	SCHEDULING_SCHEME = 0; // use a fifo IO scheduler during the actual experiment
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
