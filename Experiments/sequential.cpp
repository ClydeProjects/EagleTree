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

	//PRINT_LEVEL = 1;

	set_normal_config();

	int IO_limit = 200000;
	double space_min = 0.6;
	double space_max = 0.6;
	double space_inc = 0.5;

	Workload_Definition* init = new Init_Workload();
	Workload_Definition* experiment = new File_System_With_Noise();

	double start_time = Experiment_Runner::wall_clock_time();

	vector<vector<ExperimentResult> > exps;
	BLOCK_MANAGER_ID = 0;
	exps.push_back( Experiment_Runner::simple_experiment(experiment, init, exp_folder + "sequential/", "sequential", IO_limit, OVER_PROVISIONING_FACTOR, space_min, space_max, space_inc) );


	SCHEDULING_SCHEME = 0;
	BLOCK_MANAGER_ID = 3;
	GREED_SCALE = 2;
	WEARWOLF_LOCALITY_THRESHOLD = BLOCK_SIZE;


	exps.push_back( Experiment_Runner::simple_experiment(experiment, init, exp_folder + "sequential/", "sequential", IO_limit, OVER_PROVISIONING_FACTOR, space_min, space_max, space_inc) );


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

