#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

/*vector<Thread*> tagging(int highest_lba) {
	BLOCK_MANAGER_ID = 3;
	ENABLE_TAGGING = true;
	return sequential_experiment(highest_lba);
}

vector<Thread*> shortest_queues(int highest_lba) {
	BLOCK_MANAGER_ID = 0;
	return sequential_experiment(highest_lba);
}

vector<Thread*> detection_LUN(int highest_lba) {
	BLOCK_MANAGER_ID = 3;
	ENABLE_TAGGING = false;
	LOCALITY_PARALLEL_DEGREE = 2;
	return sequential_experiment(highest_lba);
}

vector<Thread*> detection_CHANNEL(int highest_lba) {
	BLOCK_MANAGER_ID = 3;
	ENABLE_TAGGING = false;
	LOCALITY_PARALLEL_DEGREE = 1;
	return sequential_experiment(highest_lba);
}

vector<Thread*> detection_BLOCK(int highest_lba) {
	BLOCK_MANAGER_ID = 3;
	ENABLE_TAGGING = false;
	LOCALITY_PARALLEL_DEGREE = 0;
	return sequential_experiment(highest_lba);
}*/

int main()
{
	string exp_folder  = "exp_sequential/";
	mkdir(exp_folder.c_str(), 0755);

	set_normal_config();

	int IO_limit = 100000;
	double space_min = OVER_PROVISIONING_FACTOR;
	double space_max = OVER_PROVISIONING_FACTOR;
	double space_inc = 0.05;

	Workload_Definition* init_workload = new File_System_With_Noise();

	string a = "/" + exp_folder;
	string calibration_file = get_current_dir_name() + a + "calibrated_state.txt";

	Experiment_Runner::calibrate_and_save(calibration_file, init_workload, false);

	Workload_Definition* experiment = new File_System_With_Noise();

	vector<vector<Experiment_Result> > exps;
	BLOCK_MANAGER_ID = 0;
	vector<Experiment_Result> res1 = Experiment_Runner::simple_experiment(experiment, exp_folder + "sequential/", "sequential", IO_limit, OVER_PROVISIONING_FACTOR, space_min, space_max, space_inc, calibration_file);
	exps.push_back(res1);

	BLOCK_MANAGER_ID = 3;
	ENABLE_TAGGING = true;
	//Experiment_Runner::calibrate_and_save(calibration_file, init_workload, true);
	// vector<ExperimentResult> er = Experiment_Runner::simple_experiment(experiment, exp_folder + "sequential/", "sequential", IO_limit, OVER_PROVISIONING_FACTOR, space_min, space_max, space_inc, calibration_file);
	//exps.push_back(er);

	//exp.push_back( Experiment_Runner::overprovisioning_experiment(detection_LUN, 	space_min, space_max, space_inc, exp_folder + "seq_detect_lun/",	"Seq Detect: LUN", IO_limit) );
	//exp.push_back( Experiment_Runner::simple_experiment(experiment, Init_Workload, space_min, space_max, space_inc, exp_folder + "oracle/",			"Oracle", IO_limit) );
	//exp.push_back( Experiment_Runner::overprovisioning_experiment(shortest_queues,	space_min, space_max, space_inc, exp_folder + "shortest_queues/",	"Shortest Queues", IO_limit) );

	Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (double i = space_min; i <= space_max; i += space_inc)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving
	return 0;
}

