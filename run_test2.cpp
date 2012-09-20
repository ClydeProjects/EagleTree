/* Copyright 2009, 2010 Brendan Tauras */

/* run_test2.cpp is part of FlashSim. */

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

/* Basic test driver
 * Brendan Tauras 2010-08-03
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"

#define SIZE 2

using namespace ssd;
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir


// problem: some of the pointers for the 6 block managers end up in the same LUNs. This is stupid.
// solution: have a method in bm_parent that returns a free block from the LUN with the shortest queue.

vector<Thread*> basic_sequential_experiment(int highest_lba, double IO_submission_rate) {
	long log_space_per_thread = highest_lba / 2;
	long max_file_size = log_space_per_thread / 4;
	long num_files = 100;

	Thread* fm1 = new File_Manager(0, log_space_per_thread, num_files, max_file_size, IO_submission_rate, 1, 1);
	Thread* fm2 = new File_Manager(log_space_per_thread + 1, log_space_per_thread * 2, num_files, max_file_size, IO_submission_rate, 2, 2);

	vector<Thread*> threads;
	threads.push_back(fm1);
	threads.push_back(fm2);
	return threads;
}

vector<Thread*> sequential_tagging(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 0;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> sequential_shortest_queues(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> sequential_detection_LUN(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 2;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> sequential_detection_CHANNEL(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*> sequential_detection_BLOCK(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 0;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}



int main()
{
	string home_folder = "/home/mkja/git/EagleTree/";
	string exp_folder  = "gc_experiment/";
	string folder      = home_folder + exp_folder;

	mkdir(folder.c_str(), 0755);

	load_config();

	/*sequential_tagging
	 * sequential_shortest_queues
		sequential_detection_LUN
		sequential_detection_CHANNEL
		sequential_detection_BLOCK*/

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 1;
	PAGE_WRITE_DELAY = 20;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 9;
	BLOCK_ERASE_DELAY = 150;



	//PRINT_LEVEL = 2;
	/*long logical_address_space_size = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.95;
	vector<Thread*> threads = sequential_tagging(logical_address_space_size, 40);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	StatisticsGatherer::get_instance()->print();
	delete os;
*/
	int space_min = 80;
	int space_max = 95;
	int space_inc = 5;

	vector<Exp> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_tagging, 			space_min, space_max, space_inc, folder + "oracle/",				"Oracle") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_shortest_queues,	space_min, space_max, space_inc, folder + "shortest_queues/",	"Shortest Queues") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_LUN, 	space_min, space_max, space_inc, folder + "seq_detect_lun/",		"Seq Detect: LUN") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_CHANNEL, space_min, space_max, space_inc, folder + "seq_detect_channel/",	"Seq Detect: CHANNEL") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_BLOCK, 	space_min, space_max, space_inc, folder + "seq_detect_block/",	"Seq Detect: BLOCK") );

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write wait, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	chdir(folder.c_str());
	Experiment_Runner::graph(sx, sy,   "Maximum sustainable throughput", "throughput", 24, exp);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
	}

	return 0;
}

