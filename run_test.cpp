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

using namespace ssd;

vector<Thread*> basic_sequential_experiment(int highest_lba, int num_IOs, double IO_submission_rate) {
	long log_space_per_thread = highest_lba / 2;
	long max_file_size = log_space_per_thread / 4;
	long num_files = 100;

	Thread* fm1 = new File_Manager(0, log_space_per_thread, num_files, max_file_size, IO_submission_rate, 1, 1);

	vector<Thread*> threads;
	threads.push_back(fm1);
	return threads;
}

vector<Thread*>  sequential_writes_greedy_gc(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*>  sequential_writes_lazy_gc(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*>  random_writes_experiment(int highest_lba, int num_IOs, double IO_submission_rate) {
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}


vector<Thread*>  random_writes_greedy_gc(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	return random_writes_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*>  random_writes_lazy_gc(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = false;
	return random_writes_experiment(highest_lba, num_IOs, IO_submission_rate);
}

int main()
{
	/*
	 * sequential_writes_lazy_gc
	 * random_writes_experiment
	 * random_writes_greedy_gc
	 * random_writes_lazy_gc
	 */
	load_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 32;
	BLOCK_SIZE = 32;
	PRINT_LEVEL = 2;

	long logical_address_space_size = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.9;

		/*sequential_tagging
		 * sequential_shortest_queues
			sequential_detection_LUN
			sequential_detection_CHANNEL
			sequential_detection_BLOCK*/

	vector<Thread*> threads = random_writes_lazy_gc(logical_address_space_size, 100000, 600);
	OperatingSystem* os = new OperatingSystem(threads);
	os->set_num_writes_to_stop_after(10000);
	os->run();
	StatisticsGatherer::get_instance()->print();
	delete os;

	return 0;
}
