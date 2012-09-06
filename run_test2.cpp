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
	StateTracer::print();
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
	StateTracer::print();
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
	StateTracer::print();
	delete os;
}

void file_manager_experiment() {
	vector<Thread*> threads;
	Thread* fm1 = new File_Manager(0, 399, 200, 100, 10, 1, 1);
	Thread* fm2 = new File_Manager(450, 799, 200, 100, 10, 2, 2);
	threads.push_back(fm1);
	threads.push_back(fm2);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateTracer::print();
	delete os;
}

void DBWorkload() {
	vector<Thread*> threads;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, 799, 1, WRITE, 4);
	//t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 799, 799, 1, WRITE, 17, 1));
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 799, 799, 1, READ, 17, 1));
	t1->add_follow_up_thread(new Asynchronous_Sequential_Thread(0, 799, 1, WRITE, 20));
	threads.push_back(t1);
	//thread_dependencies[0].push(new Asynchronous_Random_Thread(0, 199, 1000, 1, WRITE, 17, 1));
	//thread_dependencies[1].push(new Asynchronous_Random_Thread(200, 199, 1000, 2, READ, 17, 2));
	//thread_dependencies[2].push(new Synchronous_Sequential_Thread(200, 399, 10, WRITE));
	//thread_dependencies[3].push(new Asynchronous_Sequential_Thread(400, 599, 1, WRITE));
	//thread_dependencies[3].push(new Asynchronous_Random_Thread(400, 599, 10, READ));
	//thread_dependencies[4].push(new External_Sort(400, 599, 40, 600, 799));
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	delete os;
}

int main()
{
	load_config();
	print_config(NULL);
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

