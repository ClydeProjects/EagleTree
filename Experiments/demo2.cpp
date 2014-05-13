#include "../ssd.h"
using namespace ssd;

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	string name  = "/demo2_output/";
	Experiment::create_base_folder(name.c_str());

	Workload_Definition* init = new Init_Workload();
	string calib_name = "calib.txt";
	SCHEDULING_SCHEME = 1;
	Experiment::calibrate_and_save(init, calib_name);
	delete init;
	Experiment* e = new Experiment();
	e->set_calibration_file(calib_name);
	e->set_io_limit(1000000);
	int deadline_min = 2;
	int deadline_max = 50;
	int incr = 10;
	e->set_variable(&GREED_SCALE, deadline_min, deadline_max, incr, "Writes Deadline (µs)");
	Workload_Definition* workload = new Asynch_Random_Workload(0.8);
	e->set_workload(workload);
	SCHEDULING_SCHEME = 6;
	e->run("fifo");
	e->draw_graphs();
	delete workload;
	delete e;
	return 0;
}
