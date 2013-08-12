#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

int main(int argc, char* argv[])
{
	printf("Running interleaving!!\n");
	char* results_file = "";
	if (argc == 1) {
		printf("Setting big SSD config\n");
		set_big_SSD();
		PAGE_READ_DELAY = 115 / 5;
		PAGE_WRITE_DELAY = 1600 / 5;
		BUS_CTRL_DELAY = 5 / 5;
		BUS_DATA_DELAY = 350 / 5;
		BLOCK_ERASE_DELAY = 3000 / 5;
	}
	else if (argc == 3) {
		char* config_file_name = argv[1];
		printf("Setting config from file:  %s\n", config_file_name);
		results_file = argv[2];
		printf("results_file:  %s\n", results_file);
		load_config(config_file_name);
	}
	string name  = "/exp_interleaving/";
	string exp_folder = get_current_dir_name() + name;

	printf("creating exp folder:  %s\n", get_current_dir_name());

	Experiment::create_base_folder(exp_folder.c_str());

	Workload_Definition* init = new Init_Workload();
	string calibration_file = "calib.txt";
	Experiment::calibrate_and_save(init, calibration_file);

	double writes_probability = 0.5;
	Workload_Definition* workload = new Asynch_Random_Workload(writes_probability);
	Experiment* e = new Experiment();
	if (argc == 3) {
		printf("Setting alternate file results location!!!!!    %s\n", results_file);
		e->set_alternate_location_for_results_file(results_file);
	}
	//e->set_alternate_location_for_results_file("/home/niv/Desktop/GUI_eagle_tree/src/");
	e->set_calibration_file(calibration_file);
	e->set_workload(workload);
	e->set_io_limit(50000);
	//e->set_generate_trace_file(false);

	if (argc == 1) {
		e->set_generate_trace_file(true);
		ALLOW_DEFERRING_TRANSFERS = false;
		e->run("no_split");
		ALLOW_DEFERRING_TRANSFERS = true;
		e->run("split");
	}
	else if (ALLOW_DEFERRING_TRANSFERS) {
		e->run("split");
	} else {
		e->run("no_split");
	}
	e->draw_graphs();
	//e->draw_experiment_spesific_graphs();
	/*if (argc == 3) {
		printf("HERHEEGWDGASGASRG!!!!!    %s\n", results_file);
		Experiment::write_results_file(results_file);
	}*/

	//ALLOW_DEFERRING_TRANSFERS = true;
	//e->run("split");

	delete workload;
	delete init;
	return 0;
}
