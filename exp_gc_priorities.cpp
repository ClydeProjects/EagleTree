/*
 * gc_vs_app_priorities_exp.cpp
 *
 *  Created on: Sep 20, 2012
 *      Author: niv
 */

#include "ssd.h"
#include <unistd.h>

using namespace ssd;

vector<Thread*>  random_writes_experiment(int highest_lba, double IO_submission_rate) {
	long num_IOs = 5000;
	Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba, num_IOs, 2, WRITE, IO_submission_rate, 1));
	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

vector<Thread*>  gc_has_priority(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = true;
	PRIORITISE_GC = true;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

vector<Thread*>  app_and_gc_have_same_priority(int highest_lba, double IO_submission_rate) {
	BLOCK_MANAGER_ID = 0;
	GREEDY_GC = false;
	PRIORITISE_GC = false;
	return random_writes_experiment(highest_lba, IO_submission_rate);
}

int main()
{
	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 1;
	PAGE_WRITE_DELAY = 20;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 9;
	BLOCK_ERASE_DELAY = 150;

	vector<Exp> exp;
	exp.push_back( Experiment_Runner::overprovisioning_experiment(app_and_gc_have_same_priority, 80, 80, 5, "/home/niv/Desktop/EagleTree/rand_greed/", "rand greed") );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(gc_has_priority, 70, 80, 5, "/home/niv/Desktop/EagleTree/rand_lazy/", "rand lazy") );



}
