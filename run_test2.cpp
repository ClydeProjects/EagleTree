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

	//Thread* sw1 = new Synchronous_Sequential_Writer(0, 20, 1);
	//Thread* sw2 = new Asynchronous_Sequential_Writer(50, 55, 1);
	Thread* sw2 = new Synchronous_Random_Writer(0, 50, 20, 1);

	vector<Thread*> threads;
	threads.push_back(sw2);
	//threads.push_back(sw2);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	for (int i = 0; i < 10; i++)
	{
		//ssd -> event_arrive(WRITE, i, 1, 1 + i * 1  );
	}

	//os.run();

	/*for (int j = 0; j < 15; j++) {
		StateTracer::print();
		for (int i = 121; i < 240; i++)
		{
			result = ssd -> event_arrive(WRITE, i, 1, j * 600 + 2000 + i * 5  );
		}
	}
	StateTracer::print();
*/

	for (int i = 0; i < 1; i++)
	{
		//int lba = rand() % (NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE / 2);
		//result = ssd -> event_arrive(READ, lba, 1, 0);
	}


	//delete ssd;

	VisualTracer::get_instance()->print();

	return 0;
}
