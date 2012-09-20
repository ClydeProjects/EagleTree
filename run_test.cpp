/* Copyright 2009, 2010 Brendan Tauras */

/* run_test.cpp is part of FlashSim. */

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
 * Brendan Tauras 2009-11-02
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"
#include <unistd.h>

using namespace ssd;

vector<Thread*> basic_sequential_experiment(int highest_lba, double IO_submission_rate) {
	long max_file_size = highest_lba / 4;
	long num_files = 200;

	Thread* fm1 = new File_Manager(0, highest_lba, num_files, max_file_size, IO_submission_rate, 1, 1);

	vector<Thread*> threads;
	threads.push_back(fm1);
	return threads;
}

vector<Thread*>  sequential_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  sequential_writes_lazy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

/*vector<Thread*>  synch_random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 2000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	double num_threads = SSD_SIZE * PACKAGE_SIZE;
	long space_per_thread = highest_lba / num_threads;

	for (uint i = 0; i < num_threads; i++) {
		long min_lba = space_per_thread * i;
		long max_lba = space_per_thread * (i + 1) - 1;
		t1->add_follow_up_thread(new Synchronous_Random_Thread(min_lba, max_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	}

	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}*/

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 50000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Collision_Free_Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}


vector<Thread*>  random_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  random_writes_lazy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = false;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	bool debug = true;
	/*
	 * sequential_writes_lazy_gc
	 * sequential_writes_greedy_gc
	 * random_writes_greedy_gc
	 * random_writes_lazy_gc
	 */
	load_config();

	PRINT_LEVEL = 1;

	if (!debug) {
		SSD_SIZE = 4;
		PACKAGE_SIZE = 2;
		DIE_SIZE = 1;
		PLANE_SIZE = 128;
		BLOCK_SIZE = 32;

		PAGE_READ_DELAY = 5;
		PAGE_WRITE_DELAY = 20;
		BUS_CTRL_DELAY = 1;
		BUS_DATA_DELAY = 8;
		BLOCK_ERASE_DELAY = 150;
		//submission_rate = 10;
	} else {
		SSD_SIZE = 4;
		PACKAGE_SIZE = 2;
		DIE_SIZE = 1;
		PLANE_SIZE = 64;
		BLOCK_SIZE = 32;

		PAGE_READ_DELAY = 5;
		PAGE_WRITE_DELAY = 20;
		BUS_CTRL_DELAY = 1;
		BUS_DATA_DELAY = 8;
		BLOCK_ERASE_DELAY = 150;
	}



		/*sequential_tagging
		 * sequential_writes_greedy_gc
			sequential_writes_lazy_gc
			sequential_detection_CHANNEL
			sequential_detection_BLOCK*/

	long logical_address_space_size = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.8;
	printf("logical_address_space_size  %d\n", logical_address_space_size);
	vector<Thread*> threads = random_writes_greedy_gc(logical_address_space_size, 100);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	StatisticsGatherer::get_instance()->print();
	delete os;
	return 0;

	//Experiment_Runner::overprovisioning_experiment(random_writes_greedy_gc, 80, 90, 5, "/home/niv/Desktop/EagleTree/rand_greed/", "rand greed");

    ////////////////////////////////////////////////

	vector<Exp> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(random_writes_greedy_gc, 60, 90, 5, "/home/niv/Desktop/EagleTree/rand_greed/", "rand greed") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(random_writes_lazy_gc, 60, 90, 5, "/home/niv/Desktop/EagleTree/rand_lazy/", "rand lazy") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_writes_greedy_gc, 60, 90, 5, "/home/niv/Desktop/EagleTree/seq_greed/", "seq greed") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_writes_lazy_gc, 60, 90, 5, "/home/niv/Desktop/EagleTree/seq_lazy/", "seq lazy") );


	// Print column names for info
	for (uint i = 0; i < exp[0].column_names.size(); i++)
		printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write wait, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	used_space_values_to_show.push_back(80);
	used_space_values_to_show.push_back(85);
	used_space_values_to_show.push_back(90);
	used_space_values_to_show.push_back(95);

	int sx = 16;
	int sy = 8;

	chdir("/home/niv/Desktop/EagleTree/");
	Experiment_Runner::graph					(sx, sy,   "Maximum sustainable throughput", "throughput", 24, exp);

	for (uint i = 0; i < exp.size(); i++) {
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
	}
	return 0;
}
