#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <sstream>

using namespace ssd;


int main()
{
	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 32;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 100;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 100000;
	int write_threads_min = 1;
	int write_threads_max = 4;

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 2;
	// DEADLINES?
	GREED_SCALE = 2;
	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;

	const double used_space = .80; // overprovisioning level for variable random write threads experimentexp_balanced_scheduler
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;
	const long noise_space_min = 0;
	const long noise_space_max = logical_space;

	vector<vector<Experiment_Result> > exps;
	string exp_folder  = "exp_balanced_scheduling/";
 	mkdir(exp_folder.c_str(), 0755);

	BALANCEING_SCHEME = false;
	MAX_CONCURRENT_GC_OPS = 6;
	exps.push_back( Experiment::random_writes_on_the_side_experiment(NULL,	write_threads_min, write_threads_max, 1, exp_folder + "Normal_Sched/", "Normal sched",       IO_limit, logical_space, noise_space_min, noise_space_max) );

	BALANCEING_SCHEME = true;
	MAX_CONCURRENT_GC_OPS = 1;
	exps.push_back( Experiment::random_writes_on_the_side_experiment(NULL,	write_threads_min, write_threads_max, 1, exp_folder + "Balanced_Sched/", "Balanced sched",   IO_limit, logical_space, noise_space_min, noise_space_max) );

	Experiment::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = write_threads_min; i <= write_threads_max; i += 1)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);

	double start_time = Experiment::wall_clock_time();
	double end_time = Experiment::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment::pretty_time(end_time - start_time).c_str());

	chdir(".."); // Leaving
	return 0;
}

