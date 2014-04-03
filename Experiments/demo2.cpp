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
	e->set_io_limit(100000);
	int deadline_min = 0;
	int deadline_max = 1000;
	int incr = 200;
	e->set_variable(&WRITE_DEADLINE, deadline_min, deadline_max, incr, "Writes Deadline (µs)");
	Workload_Definition* workload = new Asynch_Random_Workload();
	e->set_workload(workload);
	SCHEDULING_SCHEME = 0;
	e->run("fifo");
	SCHEDULING_SCHEME = 2;
	e->run("reads prioritized");
	e->draw_graphs();
	delete workload;
	delete e;
	return 0;
}
