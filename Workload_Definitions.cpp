
#include "ssd.h"
#include "scheduler.h"
#include "Operating_System.h"

using namespace ssd;

//*****************************************************************************************
//				Workload_Definition
//*****************************************************************************************

Workload_Definition::Workload_Definition() :
		min_lba(0),
		max_lba(OVER_PROVISIONING_FACTOR * NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE)
{}

void Workload_Definition::recalculate_lba_range() {
	min_lba = 0;
	max_lba = OVER_PROVISIONING_FACTOR * NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
}

vector<Thread*> Workload_Definition::generate_instance() {
	recalculate_lba_range();
	return generate();
}

//*****************************************************************************************
//				GRACE HASH JOIN WORKLOAD
//*****************************************************************************************
Grace_Hash_Join_Workload::Grace_Hash_Join_Workload()
 : r1(0.2), r2(0.2), fs(0.6), use_flexible_reads(false) {}

vector<Thread*> Grace_Hash_Join_Workload::generate() {
	Grace_Hash_Join::initialize_counter();

	int relation_1_start = min_lba;
	int relation_1_end = min_lba + (max_lba - min_lba) * r1;
	int relation_2_start = relation_1_end + 1;
	int relation_2_end = relation_2_start + (max_lba - min_lba) * r2;
	int temp_space_start = relation_2_end + 1;
	int temp_space_end = max_lba;

	Thread* first = new Asynchronous_Sequential_Trimmer(temp_space_start, temp_space_end);
	Thread* preceding_thread = first;
	for (int i = 0; i < 1000; i++) {
		Thread* grace_hash_join = new Grace_Hash_Join(	relation_1_start,	relation_1_end,
				relation_2_start,	relation_2_end,
				temp_space_start, temp_space_end,
				use_flexible_reads, false, 32, 31 * i + 1);

		if (i == 0) grace_hash_join->set_time_to_wait_before_starting(10000);

		grace_hash_join->set_experiment_thread(true);
		preceding_thread->add_follow_up_thread(grace_hash_join);
		preceding_thread = grace_hash_join;
	}
	vector<Thread*> threads(1, first);
	return threads;
}

//*****************************************************************************************
//				Synch RANDOM WORKLOAD
//*****************************************************************************************
Random_Workload::Random_Workload(long num_threads)
 : num_threads(num_threads) {}

vector<Thread*> Random_Workload::generate() {
	Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	for (int i = 0; i < num_threads; i++) {
		int seed = 23621 * i + 62;
		Simple_Thread* writer = new Synchronous_Random_Writer(min_lba, max_lba / 2, seed);
		Simple_Thread* reader = new Synchronous_Random_Reader(min_lba, max_lba / 2, seed * 136);
		writer->set_experiment_thread(true);
		reader->set_experiment_thread(true);
		init_write->add_follow_up_thread(reader);
		init_write->add_follow_up_thread(writer);
		writer->set_num_ios(INFINITE);
		reader->set_num_ios(INFINITE);
	}
	vector<Thread*> threads(1, init_write);
	return threads;
}

//*****************************************************************************************
//				Asynch RANDOM WORKLOAD
//*****************************************************************************************
Asynch_Random_Workload::Asynch_Random_Workload()
 : stats(new StatisticsGatherer()) {}

Asynch_Random_Workload::~Asynch_Random_Workload() {
	//stats->print();
	VisualTracer::get_instance()->print_horizontally(10000);
	delete stats;
}

vector<Thread*> Asynch_Random_Workload::generate() {
	//Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	Asynchronous_Random_Thread_Reader_Writer* thread = new Asynchronous_Random_Thread_Reader_Writer(min_lba, max_lba, INFINITE, 236);
	thread->set_statistics_gatherer(stats);
	thread->set_experiment_thread(true);
	//init_write->add_follow_up_thread(thread);
	//init_write->set_statistics_gatherer(stats);
	//init_write->set_experiment_thread(true);
	vector<Thread*> threads(1, thread);
	return threads;
}

//*****************************************************************************************
//				Classical INIT workload
//*****************************************************************************************
Init_Workload::Init_Workload() {}

vector<Thread*> Init_Workload::generate() {
	Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	Simple_Thread* thread = new Asynchronous_Random_Writer(min_lba, max_lba, 23623);
	thread->set_num_ios(NUMBER_OF_ADDRESSABLE_PAGES() * 2);
	//thread->set_num_ios(1000);
	init_write->add_follow_up_thread(thread);
	//init_write->set_statistics_gatherer(stats);
	//init_write->set_experiment_thread(true);
	vector<Thread*> threads(1, init_write);
	return threads;
}

//*****************************************************************************************
//				SYNCH SEQUENTIAL WRITE
//*****************************************************************************************
Synch_Write::Synch_Write()
 : stats(new StatisticsGatherer()) {}

Synch_Write::~Synch_Write() {
	stats->print();
	VisualTracer::get_instance()->print_horizontally(10000);
	delete stats;
}

vector<Thread*> Synch_Write::generate() {
	/*Simple_Thread* init_write = new Synchronous_Sequential_Writer(min_lba, max_lba);
	//init_write->add_follow_up_thread(thread);
	init_write->set_statistics_gatherer(stats);
	init_write->set_experiment_thread(true);
	vector<Thread*> threads(1, init_write);
	return threads;*/


	Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	int seed = 235325;

	Simple_Thread* random_format = new Asynchronous_Random_Writer(min_lba, max_lba, seed);
	random_format->set_num_ios((max_lba - min_lba) * 3);
	init_write->add_follow_up_thread(random_format);

	int num_files = 1000;
	int max_file_size = 100;
	Thread* fm = new File_Manager(min_lba, max_lba / 2, num_files, max_file_size, seed * 13);
	int num_ios = 10000;
	Thread* rand = new Asynchronous_Random_Thread_Reader_Writer(max_lba / 2 + 1, max_lba, num_ios, seed * 17);
	random_format->add_follow_up_thread(fm);
	random_format->add_follow_up_thread(rand);

	vector<Thread*> threads(1, init_write);
	return threads;
}

//*****************************************************************************************
//				File System With Noise
//*****************************************************************************************

File_System_With_Noise::File_System_With_Noise() {

}

vector<Thread*> File_System_With_Noise::generate() {

	long log_space_per_thread = max_lba / 4;
	long max_file_size = log_space_per_thread / 7;

	Thread* experiment_thread1 = new Asynchronous_Random_Writer(0, log_space_per_thread * 2, 472);
	Thread* experiment_thread2 = new File_Manager(log_space_per_thread * 2 + 1, log_space_per_thread * 3, 1000000, max_file_size, 713);
	Thread* experiment_thread3 = new File_Manager(log_space_per_thread * 3 + 1, log_space_per_thread * 4, 1000000, max_file_size, 5);

	experiment_thread1->set_experiment_thread(true);
	experiment_thread2->set_experiment_thread(true);
	experiment_thread3->set_experiment_thread(true);

	vector<Thread*> threads;
	threads.push_back(experiment_thread1);
	threads.push_back(experiment_thread2);
	threads.push_back(experiment_thread3);

	return threads;
}

