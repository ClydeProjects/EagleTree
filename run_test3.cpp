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
    PRINT_LEVEL = 1;
	vector<Thread*> threads;
	Thread* t1 = new Asynchronous_Random_Writer(0, 99, 457); // 799
	Thread* t2 = new Synchronous_Sequential_Writer(100, 199); // 799
	//threads.push_back(t1);
	t2->add_follow_up_thread(t1);
	threads.push_back(t2);
	OperatingSystem* os = new OperatingSystem(threads);
	os->run();
	//VisualTracer::get_instance()->print_horizontally_with_breaks();
	delete os;

	//VisualTracer::get_instance()->print_horizontally_with_breaks();

	return 0;

	printf("----------------------------------------------------\n");

	Ssd *ssd = new Ssd();
	//os->set_ssd(ssd);
	//os->run();

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

	//VisualTracer::get_instance()->print_horizontally_with_breaks();

	return 0;
}
