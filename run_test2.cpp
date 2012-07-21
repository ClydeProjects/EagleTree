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
	{	load_config();
	print_config(NULL);
   printf("Press ENTER to continue...");
   //getchar();
   printf("\n");



	double cur_time = 1;
	double delta = BUS_DATA_DELAY - 2 > 0 ? BUS_DATA_DELAY - 2 : BUS_DATA_DELAY;

	for (int i = 0; i < 20; i++)
	{
		//ssd -> event_arrive(WRITE, i, 1, 1 + i * 5  );
	}
	//StateTracer::print();

	Thread* sw = new Synchronous_Writer(0, 100, 2);

	vector<Thread*> threads;
	threads.push_back(sw);

	OperatingSystem* os = new OperatingSystem(threads);

	Ssd *ssd = new Ssd(*os);
	os->set_ssd(ssd);
	os->run();

	for (int i = 0; i < 2; i++)
	{
		//ssd -> event_arrive(WRITE, i, 1, 1 + i * 5  );
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

	//result = ssd -> event_arrive(WRITE, 100, 1, 200 );
	//result = ssd -> event_arrive(WRITE, 100, 1, 1000 );


	/*for (int i = 0; i < 1000; i++)
	{
		result = ssd -> event_arrive(READ, i, 1, 200 );
	}*/

	for (int i = 0; i < 1; i++)
	{
		//int lba = rand() % (NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE / 2);
		//result = ssd -> event_arrive(READ, lba, 1, 0);
	}

	/*for (int i = 0; i < 50; i++, cur_time += delta)
	{
		result = ssd -> event_arrive(WRITE, i, 1, 10000 + 100 * i);
	}

	for (int i = 0; i < 50; i++, cur_time += delta)
	{
		result = ssd -> event_arrive(WRITE, i, 1, 15000 + 100 * i);
	}

	for (int i = 0; i < 50; i++, cur_time += delta)
	{
		result = ssd -> event_arrive(WRITE, i, 1, 20000 + 100 * i);
	}*/

	//result = ssd -> event_arrive(WRITE, 4, 1, 1 + 28 * 200);
	//result = ssd -> event_arrive(WRITE, 5, 1, 1 + 28 * 200);

	/*for (int i = 0; i < 10; i++, cur_time += delta)
	{
		result = ssd -> event_arrive(WRITE, 7, 1, 3000 + i * 200);
	}*/

	//result = ssd -> event_arrive(WRITE, 1, 1, 1);
	for (int i = 0; i < SIZE; i++, cur_time += delta)
	{
		/* event_arrive(event_type, logical_address, size, start_time) */
		//result = ssd -> event_arrive(READ, 1, 1, cur_time);
		//result = ssd -> event_arrive(READ, i, 1, cur_time);
	}

	delete ssd;

	VisualTracer::get_instance()->print();

	return 0;
}
