#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <sstream>

using namespace ssd;

Workload_Definition* grace_hash_join(int highest_lba) {
	Grace_Hash_Join_Workload* workload = new Grace_Hash_Join_Workload(0, highest_lba);
	workload->set_use_flexible_Reads(false);
	MAX_CONCURRENT_GC_OPS = 6;
	return workload;
}

Workload_Definition* grace_hash_join_flex(int highest_lba) {
	Grace_Hash_Join_Workload* workload = new Grace_Hash_Join_Workload(0, highest_lba);
	workload->set_use_flexible_Reads(true);
	MAX_CONCURRENT_GC_OPS = 6;
	return workload;
}

/*vector<Thread*> grace_hash_join_balancing(int highest_lba) {
	BALANCEING_SCHEME = true;
	MAX_CONCURRENT_GC_OPS = 1;
	return grace_hash_join(highest_lba, false, false, 1);
}

vector<Thread*> grace_hash_join_flex_balancing(int highest_lba) {
	BALANCEING_SCHEME = true;
	MAX_CONCURRENT_GC_OPS = 1;
	return grace_hash_join(highest_lba, true, false, 1);
}*/


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

	int IO_limit = 10000;

	int write_threads_min = 0;
	int write_threads_max = 2;

	//PRINT_LEVEL = 1;
	MAX_SSD_QUEUE_SIZE = 10;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 2;
	// DEADLINES?
	GREED_SCALE = 2;
	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;

	const double used_space = .80; // overprovisioning level for variable random write threads experiment
	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int logical_space = num_pages * used_space;
	const long grace_space = logical_space * 0.5;
	const long noise_space_min = grace_space + logical_space * 0.5;
	const long noise_space_max = logical_space;

	vector<vector<ExperimentResult> > exps;
	string exp_folder  = "exp_grace_hash_join/";
 	mkdir(exp_folder.c_str(), 0755);

	exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_flex(grace_space),	  write_threads_min, write_threads_max, 1, exp_folder + "Flexible_reads/", "Flexible reads",        IO_limit, logical_space, noise_space_min, noise_space_max) );
	exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join(grace_space),		  write_threads_min, write_threads_max, 1, exp_folder + "None/", "None",                  			IO_limit, logical_space, noise_space_min, noise_space_max) );
	//exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_flex_balancing,	  write_threads_min, write_threads_max, 1, exp_folder + "Flexible_reads_bal/", "Flexible reads bal",        IO_limit, used_space, avail_pages*ns+2, avail_pages) );
	//exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_balancing,		  write_threads_min, write_threads_max, 1, exp_folder + "None_bal/", "None bal",                  			IO_limit, used_space, avail_pages*ns+2, avail_pages) );

	Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show;
	for (int i = write_threads_min; i <= write_threads_max; i += 1)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);

	double start_time = Experiment_Runner::wall_clock_time();
	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	chdir(".."); // Leaving
	return 0;
}

