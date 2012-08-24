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
void experiement1() {
	vector<Thread*> threads;
	threads.push_back(new Asynchronous_Random_Thread(0, 199, 108, 1, WRITE, 10, 1));
	/*threads.push_back(new Asynchronous_Sequential_Thread(200, 399, 1, WRITE));
	threads.push_back(new Asynchronous_Sequential_Thread(400, 599, 1, WRITE));
	threads.push_back(new Asynchronous_Sequential_Thread(600, 799, 1, WRITE));*/
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateTracer::print();
	delete os;
}

void experiement2() {

	Thread* t1 = new Asynchronous_Sequential_Thread(0, 50, 1, WRITE, 17);
	Thread* t2 = new Asynchronous_Sequential_Thread(200, 250, 1, WRITE, 17);
	Thread* t3 = new Asynchronous_Sequential_Thread(400, 450, 1, WRITE, 17);
	Thread* t4 = new Asynchronous_Sequential_Thread(600, 650, 1, WRITE, 17);

	//t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 199, 1000, 1, WRITE, 100, 1));
	//t2->add_follow_up_thread(new Asynchronous_Random_Thread(200, 399, 1000, 2, WRITE, 100, 2));
	//t3->add_follow_up_thread(new Asynchronous_Random_Thread(400, 599, 1000, 3, WRITE, 100, 3));
	//t4->add_follow_up_thread(new Asynchronous_Random_Thread(600, 799, 1000, 4, WRITE, 100, 4));

	vector<Thread*> threads;
	threads.push_back(t1);
	threads.push_back(t2);
	threads.push_back(t3);
	threads.push_back(t4);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateTracer::print();
	delete os;
}

void file_manager_experiment() {
	vector<Thread*> threads;
	Thread* fm = new File_Manager(0, 800, 20, 10, 1);
	threads.push_back(fm);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	VisualTracer::get_instance()->print_horizontally_with_breaks();
	StateTracer::print();
	delete os;
}

void niv_experiment() {
	vector<Thread*> threads;
	Thread* t1 = new Synchronous_Sequential_Thread(0, 80, 1, WRITE, 1);
	Thread* t2 = new Synchronous_Sequential_Thread(200, 399, 1, WRITE, 2);
	Thread* t3 = new Synchronous_Sequential_Thread(400, 599, 1, WRITE, 3);
	Thread* t4 = new Synchronous_Sequential_Thread(600, 799, 1, WRITE, 4);

	/*t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, 80, 500, 1, WRITE, 20, 1));
	t2->add_follow_up_thread(new Asynchronous_Random_Thread(200, 399, 500, 1, WRITE, 20, 1));
	t3->add_follow_up_thread(new Asynchronous_Random_Thread(400, 599, 500, 1, WRITE, 20, 1));
	t4->add_follow_up_thread(new Asynchronous_Random_Thread(600, 799, 500, 1, WRITE, 20, 1));*/

	threads.push_back(t1);
	threads.push_back(t2);
	threads.push_back(t3);
	threads.push_back(t4);

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

	file_manager_experiment();

	// this experiment already shows huge disparity

	/*threads.push_back(new Synchronous_Sequential_Writer(0, 40, 1));
	threads.push_back(new Synchronous_Sequential_Writer(40, 80, 1));
	threads.push_back(new Synchronous_Sequential_Writer(80, 120, 1));
	threads.push_back(new Synchronous_Sequential_Writer(120, 160, 10));
	threads.push_back(new Synchronous_Sequential_Writer(160, 200, 10));*/



	/*vector<queue<Thread*> > thread_dependencies(2, queue<Thread*>());

	thread_dependencies[0].push(new Asynchronous_Sequential_Thread(0, 49, 1, WRITE));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));

	thread_dependencies[1].push(new Asynchronous_Sequential_Thread(100, 189, 1, WRITE));
	thread_dependencies[1].push(new Asynchronous_Sequential_Thread(100, 189, 10, READ));
	thread_dependencies[1].push(new Asynchronous_Sequential_Thread(100, 189, 1, WRITE));*/
	//thread_dependencies[1].push(new Asynchronous_Sequential_Thread(100, 189, 10, READ));


	//thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));
	//thread_dependencies[0].push(new External_Sort(0, 49, 10, 50, 99));




	/*thread_dependencies[0].push(new Asynchronous_Sequential_Writer(0, 49, 1));

	threads.push_back(new Asynchronous_Sequential_Writer(0, 49, 1));
	threads.push_back(new External_Sort(0, 49, 10, 50, 99, 500));*/



	//threads.push_back(new External_Sort(0, 49, 10, 50, 99, 3000));
	//threads.push_back(new Asynchronous_Sequential_Writer(100, 110, 1, 3000));



	for (int i = 0; i < 10; i++)
	{
		//ssd -> event_arrive(WRITE, i, 1, 1 + i * 1  );
	}

	//os.run();


	//VisualTracer::get_instance()->print_horizontally_with_breaks();
	//StateTracer::print();
	//StatisticsGatherer::get_instance()->print();
	return 0;
}

