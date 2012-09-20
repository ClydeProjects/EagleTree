/*
 * exp_load_balancing.cpp
 *
 *  Created on: Sep 20, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

vector<Thread*> basic_sequential_experiment(int highest_lba, double IO_submission_rate) {
	long max_file_size = highest_lba / 4;
	long num_files = 1;

	Thread* fm1 = new File_Manager(0, highest_lba, num_files, max_file_size, IO_submission_rate, 1, 1);

	vector<Thread*> threads;
	threads.push_back(fm1);
	return threads;
}

vector<Thread*>  sequential_writes_greedy_gc(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 0;
	ENABLE_TAGGING = false;
	return basic_sequential_experiment(highest_lba, IO_submission_rate);
}

int main()
{

	load_config();

	PRINT_LEVEL = 0;
	PRINT_FILE_MANAGER_INFO = true;

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 1;
	PAGE_WRITE_DELAY = 20;
	BUS_CTRL_DELAY = 1;
	BUS_DATA_DELAY = 9;
	BLOCK_ERASE_DELAY = 150;

	long address_space = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.9;
	vector<Thread*> threads = sequential_writes_greedy_gc(address_space, 40);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	StatisticsGatherer::get_instance()->print();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	delete os;
}
