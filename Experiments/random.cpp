#include "../ssd.h"
using namespace ssd;

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	PRINT_LEVEL = 0;
	FTL_DESIGN = 0;
	OVER_PROVISIONING_FACTOR = 0.7;
	string name  = "/random/";
	Experiment::create_base_folder(name.c_str());
	Workload_Definition* init = new Init_Workload();

	string calibration_file = "calib.txt";
	SCHEDULING_SCHEME = 0; // use the noop IO scheduler during calibration because it's fastest in terms of real execution time
	GARBAGE_COLLECTION_POLICY = 0;
	//Experiment::calibrate_and_save(init, calibration_file);
	//delete init;
	Experiment* e = new Experiment();
	//e->set_calibration_file(calibration_file);
	Workload_Definition* workload = new Asynch_Random_Workload(1);	// bug with 0.9. fix
	e->set_workload(init);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 4);
	SCHEDULING_SCHEME = 1; // use a fifo IO scheduler during the actual experiment
	e->run("test");
	e->draw_graphs();
	//delete workload;
	return 0;
}
