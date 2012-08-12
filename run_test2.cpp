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



int main()
{
	load_config();
	print_config(NULL);
	printf("Press ENTER to continue...");
	//getchar();
	printf("\n");

	vector<Thread*> threads;
	threads.push_back(new Synchronous_Sequential_Writer(0, 40, 1));
	threads.push_back(new Synchronous_Sequential_Writer(40, 80, 1));
	threads.push_back(new Synchronous_Sequential_Writer(80, 120, 1));
	threads.push_back(new Synchronous_Sequential_Writer(120, 160, 10));
	threads.push_back(new Synchronous_Sequential_Writer(160, 200, 10));

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	for (int i = 0; i < 10; i++)
	{
		//ssd -> event_arrive(WRITE, i, 1, 1 + i * 1  );
	}

	//os.run();



	//delete ssd;


	VisualTracer::get_instance()->print();
	StateTracer::print();
	StatisticsGatherer::get_instance()->print();
	return 0;
}
