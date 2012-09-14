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

	Thread* t1 = new Asynchronous_Sequential_Thread(0, 127, 1, WRITE, 1);
	Thread* t2 = new Asynchronous_Sequential_Thread(128, 256, 1, WRITE, 1);
	//Thread* t3 = new Asynchronous_Sequential_Thread(400, 599, 1, WRITE, 17);
	//Thread* t4 = new Asynchronous_Sequential_Thread(600, 799, 1, WRITE, 17);

	//t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 64, 100, 1, WRITE, 30, 1));
	t1->add_follow_up_thread(new Asynchronous_Sequential_Thread(0, 256, 100, WRITE, 1));

	t2->add_follow_up_thread(new Asynchronous_Random_Thread(0, 256, 1000, 2, WRITE, 256, 1));
	//t3->add_follow_up_thread(new Asynchronous_Random_Thread(400, 599, 1000, 3, WRITE, 30, 3));
	//t4->add_follow_up_thread(new Asynchronous_Random_Thread(600, 799, 1000, 4, WRITE, 100, 4));

	vector<Thread*> threads;
	threads.push_back(t1);
	threads.push_back(t2);
	//threads.push_back(t3);
	//threads.push_back(t4);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateVisualiser::print_page_status();
	delete os;
}

void file_manager_experiment() {
	BLOCK_MANAGER_ID = 3;
	PRINT_LEVEL = 2;
	vector<Thread*> threads;
	long logical_space_size = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE * 0.8;
	long max_file_size = logical_space_size / 10;
	long num_files = 10;
	Thread* fm1 = new File_Manager(0, logical_space_size / 2, num_files, max_file_size, 10, 1, 1);
	Thread* fm2 = new File_Manager((logical_space_size / 2) + 1, logical_space_size, num_files, max_file_size, 10, 2, 2);
	threads.push_back(fm1);
	//threads.push_back(fm2);
	OperatingSystem* os = new OperatingSystem(threads);
	//os->set_num_writes_to_stop_after(5000);
	os->run();
	fm1->print_thread_stats();
	fm2->print_thread_stats();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateVisualiser::print_page_status();
	delete os;
}

int main()
{
	load_config();
	//print_config(NULL);
	printf("Press ENTER to continue...");
	//getchar();
	printf("\n");

//	file_manager_experiment();
//	experiment2();
//	simple_experiment();
	file_manager_experiment();
	//experiment1();
	return 0;
}

