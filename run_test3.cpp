/* Copyright 2009, 2010 Brendan Tauras */

/* run_test3.cpp is part of FlashSim. */

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

/* Basic simple interactive user write test
 * Martin Svendsen 2012-07-18 */

#include "ssd.h"
#include <iostream>
#include <sstream>

using namespace ssd;

int main() {
	load_config();
	print_config(NULL);
    printf("\n");

//	Ssd *ssd = new Ssd();
	//Thread* sw1 = new Synchronous_Sequential_Writer(0, 20, 1);
	//Thread* sw2 = new Asynchronous_Sequential_Writer(50, 70, 1);
	Thread* sw3 = new Synchronous_Random_Writer(0, 10, 100, time(NULL));

	vector<Thread*> threads;
	//threads.push_back(sw2);
	threads.push_back(sw3);

	OperatingSystem* os = new OperatingSystem(threads);
	os->run();

	Ssd *ssd = new Ssd();

    printf("Max LBA address: %d\n", SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

    int user_input_address;
	string user_input;
	int time = 0;
	double result;
	bool done = false;
	do {
		printf("Write to LBA: ");
		getline (cin, user_input);
		if (! (stringstream(user_input) >> user_input_address)) break;
		ssd -> event_arrive(WRITE, user_input_address, 1, time );
		time += 5;
	} while (!done);

	delete ssd;

	VisualTracer::get_instance()->print();

	return 0;
}
