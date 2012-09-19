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

// this experiement is to show
void experiment1() {
	vector<Thread*> threads;
	threads.push_back(new Asynchronous_Random_Thread(0, 199, 108, 1, WRITE, 10, 1));
	threads.push_back(new Asynchronous_Random_Thread(200, 399, 108, 1, WRITE, 10, 1));
	threads.push_back(new Asynchronous_Random_Thread(400, 599, 108, 1, WRITE, 10, 1));
	threads.push_back(new Asynchronous_Random_Thread(600, 799, 108, 1, WRITE, 10, 1));
	/*threads.push_back(new Asynchronous_Sequential_Thread(200, 399, 1, WRITE));
	threads.push_back(new Asynchronous_Sequential_Thread(400, 599, 1, WRITE));
	threads.push_back(new Asynchronous_Sequential_Thread(600, 799, 1, WRITE));*/
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateVisualiser::print_page_status();
	delete os;
}

void experiment2() {

	Thread* t1 = new Asynchronous_Sequential_Thread(0, 199, 1, WRITE, 17);
	Thread* t2 = new Asynchronous_Sequential_Thread(200, 399, 1, WRITE, 17);
	Thread* t3 = new Asynchronous_Sequential_Thread(400, 599, 1, WRITE, 17);
	//Thread* t4 = new Asynchronous_Sequential_Thread(600, 799, 1, WRITE, 17);

	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 199, 1000, 1, WRITE, 30, 1));
	t2->add_follow_up_thread(new Asynchronous_Random_Thread(200, 399, 1000, 2, WRITE, 30, 2));
	t3->add_follow_up_thread(new Asynchronous_Random_Thread(400, 599, 1000, 3, WRITE, 30, 3));
	//t4->add_follow_up_thread(new Asynchronous_Random_Thread(600, 799, 1000, 4, WRITE, 100, 4));

	vector<Thread*> threads;
	threads.push_back(t1);
	threads.push_back(t2);
	threads.push_back(t3);
	//threads.push_back(t4);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateVisualiser::print_page_status();
	delete os;
}

void simple_experiment() {
	GREEDY_GC = true;
	long logical_space_size = SSD_SIZE * PACKAGE_SIZE * PLANE_SIZE * BLOCK_SIZE * 0.5;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, logical_space_size, 1, WRITE, 50, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, logical_space_size, 10000, 2, WRITE, 70, 1));
	//t3->add_follow_up_thread(new Asynchronous_Random_Thread(400, 599, 1000, 3, WRITE, 30, 3));
	//t4->add_follow_up_thread(new Asynchronous_Random_Thread(600, 799, 1000, 4, WRITE, 100, 4));

	vector<Thread*> threads;
	threads.push_back(t1);
	//threads.push_back(t3);
	//threads.push_back(t4);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	//VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateVisualiser::print_page_status();
	StatisticsGatherer::get_instance()->print();
	delete os;
}

// problem: some of the pointers for the 6 block managers end up in the same LUNs. This is stupid.
// solution: have a method in bm_parent that returns a free block from the LUN with the shortest queue.

vector<Thread*> basic_sequential_experiment(int highest_lba, int num_IOs, double IO_submission_rate) {
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

vector<Thread*> sequential_tagging(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = true;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 0;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*> sequential_shortest_queues(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*> sequential_detection_LUN(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 2;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*> sequential_detection_CHANNEL(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 1;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}

vector<Thread*> sequential_detection_BLOCK(int highest_lba, int num_IOs, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 3;
	GREEDY_GC = false;
	ENABLE_TAGGING = false;
	WEARWOLF_LOCALITY_THRESHOLD = 10;
	LOCALITY_PARALLEL_DEGREE = 0;
	return basic_sequential_experiment(highest_lba, num_IOs, IO_submission_rate);
}



int main()
{
	load_config();

	/*sequential_tagging
	 * sequential_shortest_queues
		sequential_detection_LUN
		sequential_detection_CHANNEL
		sequential_detection_BLOCK*/

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 32;
	BLOCK_SIZE = 32;
	PRINT_LEVEL = 1;

	long logical_address_space_size = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.9;
	vector<Thread*> threads = sequential_detection_BLOCK(logical_address_space_size, -1, 10);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	StatisticsGatherer::get_instance()->print();
	delete os;


	vector<Exp> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_tagging,			"/home/mkja/git/EagleTree/Exp2/", "Oracle") );
	//exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_shortest_queues,	"/home/mkja/git/EagleTree/Exp3/", "Shortest Queues") );
	//exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_LUN,	"/home/mkja/git/EagleTree/Exp4/", "Seq Detect: LUN") );
	//exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_CHANNEL,"/home/mkja/git/EagleTree/Exp5/", "Seq Detect: CHANNEL") );
	//exp.push_back( Experiment_Runner::overprovisioning_experiment(sequential_detection_BLOCK,  "/home/mkja/git/EagleTree/Exp6/", "Seq Detect: BLOCK") );

	chdir("/home/mkja/git/EagleTree/Graphs");
	Experiment_Runner::graph("Testgraphtitle", "testgraph.eps", 14, exp, 16, 8);
	Experiment_Runner::graph("Testgraphtitle", "testgraph-dev.eps", 11, exp, 16, 8);

	return 0;
}

