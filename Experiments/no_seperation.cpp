#include "../ssd.h"
using namespace ssd;

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	GREED_SCALE = 4;
	BLOCK_MANAGER_ID = 0;
	//PLANE_SIZE = 1024 * 16;
	PRINT_LEVEL = 0;
	FTL_DESIGN = 0;
	string name  = "/no_seperation/";
	Experiment::create_base_folder(name.c_str());
	K2_Modal_Workload* init = new K2_Modal_Workload(1, 1);
	//string calibration_file = "calib.txt";
	SCHEDULING_SCHEME = 1; // use the noop IO scheduler during calibration because it's fastest in terms of real execution time
	//Experiment::calibrate_and_save(init, calibration_file, NUMBER_OF_ADDRESSABLE_PAGES() * 10);
	//delete init;
	Experiment* e = new Experiment();
	e->set_exponential_increase(true);
	e->set_calibration_workload(init);
	//e->set_calibration_file(calibration_file);
	//Workload_Definition* workload = new K_Modal_Workload();	// bug with 0.9. fix
	//e->set_workload(workload);

	/*double min = 0.125;
	double max = 8;
	double incr = 2;*/
	double min = 0.125;
	double max = 8;
	double incr = 2;
	e->set_variable(&init->relative_prob, min, max, incr, "Relative update frequency");

	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 1);
	SCHEDULING_SCHEME = 1; // use a fifo IO scheduler during the actual experiment
	init->relative_size = 1;
	e->run("1");
	init->relative_size = 2;
	e->run("2");
	init->relative_size = 4;
	e->run("4");
	init->relative_size = 8;
	e->run("8");
	init->relative_size = 16;
	e->run("16");
	e->draw_graphs();
	//delete workload;
	return 0;
}
