/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/


#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <sstream>

using namespace ssd;

// Internal division of space in each grace hash join instance
double r1 = .20; // Relation 1 percentage use of addresses
double r2 = .20; // Relation 2 percentage use of addresses
double fs = .60; // Free space percentage use of addresses

// Overall division of space
double gh = .5; // Grace hash join(s) percentage use of addresses
double ns = .5; // "Noise space" percentage use of addresses
//------------;
// total  =1.0

StatisticsGatherer* read_statistics_gatherer;

vector<Thread*> grace_hash_join(int highest_lba, bool use_flexible_reads, bool use_tagging, int grace_hash_join_threads = 1) {
	Grace_Hash_Join::initialize_counter();

	int relation_1_start = 0;
	int relation_1_end = highest_lba * gh * r1;
	int relation_2_start = relation_1_end + 1;
	int relation_2_end = relation_2_start + highest_lba * gh * r2;
	int temp_space_start = relation_2_end + 1;
	int temp_space_end = temp_space_start + highest_lba * gh * fs;
	int noise_space_start = temp_space_end + 1;
	int noise_space_end = highest_lba;

	Thread* first = new Grace_Hash_Join(	relation_1_start,	relation_1_end,
			relation_2_start,	relation_2_end,
			temp_space_start, temp_space_end,
			use_flexible_reads, use_tagging, 32, 462);

	for (int gt = 0; gt < grace_hash_join_threads; gt++) {
		Thread* preceding_thread = first;
		for (int i = 0; i < 1000; i++) {
			Thread* grace_hash_join = new Grace_Hash_Join(	relation_1_start,	relation_1_end,
					relation_2_start,	relation_2_end,
					temp_space_start, temp_space_end,
					use_flexible_reads, use_tagging, 32, 462);

			grace_hash_join->set_experiment_thread(true);
			preceding_thread->add_follow_up_thread(grace_hash_join);
			preceding_thread = grace_hash_join;
		}
	}
	vector<Thread*> threads;
	threads.push_back(first);

	return threads;
}

vector<Thread*> grace_hash_join(int highest_lba) {
	BLOCK_MANAGER_ID = 0;
	return grace_hash_join(highest_lba, false, false, 1);
}

vector<Thread*> grace_hash_join_flex(int highest_lba) {
	BLOCK_MANAGER_ID = 0;
	return grace_hash_join(highest_lba, true, false, 1);
}

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

	int IO_limit = 1000;

	int write_threads_min = 0;
	int write_threads_max = 1;
	double used_space = .80; // overprovisioning level for variable random write threads experiment

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 2;
	// DEADLINES?
	GREED_SCALE = 2;
	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;

	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int avail_pages = num_pages * used_space;

	vector<vector<ExperimentResult> > exps;
	string exp_folder  = "exp_grace_hash_join/";
	mkdir(exp_folder.c_str(), 0755);
	exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_flex,	  write_threads_min, write_threads_max, 1, exp_folder + "Flexible_reads/", "Flexible reads",        IO_limit, used_space, avail_pages*ns+1, avail_pages) );
	exps.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join,		  write_threads_min, write_threads_max, 1, exp_folder + "None/", "None",                  			IO_limit, used_space, avail_pages*ns+1, avail_pages) );

	Experiment_Runner::draw_graphs(exps, exp_folder);

	for (int i = 0; i < exps[0][0].column_names.size(); i++) {
		//printf("%d: %s\n", i, exps[0][0].column_names[i].c_str());
	}

	vector<int> num_write_thread_values_to_show;
	for (int i = write_threads_min; i <= write_threads_max; i += 1)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	double start_time = Experiment_Runner::wall_clock_time();
	uint mean_pos_in_datafile = std::find(exps[0][0].column_names.begin(), exps[0][0].column_names.end(), "Write latency, mean (µs)") - exps[0][0].column_names.begin();
	assert(mean_pos_in_datafile != exps[0][0].column_names.size());

	for (uint j = 0; j < exps.size(); j++) {
		vector<ExperimentResult>& exp = exps[j];
//		vector<ExperimentResult>& exp = exps[0]; // Global one
		for (uint i = 0; i < exp.size(); i++) {
			printf("%s\n", exp[i].data_folder.c_str());
			if (chdir(exp[i].data_folder.c_str()) != 0) printf("Error changing dir to %s\n", exp[i].data_folder.c_str());
			Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
			Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], num_write_thread_values_to_show, 1, 4);
			Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], num_write_thread_values_to_show, true);
			Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], num_write_thread_values_to_show);
			Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], num_write_thread_values_to_show);
			Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], num_write_thread_values_to_show);
/*			if (i == 0) // Global
				chdir("..");
			else
				chdir("../..");*/
		}
	}
	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	chdir(".."); // Leaving
	return 0;
}

