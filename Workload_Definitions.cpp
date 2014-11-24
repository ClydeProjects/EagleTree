
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
	max_lba = OVER_PROVISIONING_FACTOR * NUMBER_OF_ADDRESSABLE_PAGES();
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
		Simple_Thread* writer = new Synchronous_Random_Writer(min_lba, max_lba, seed);
		Simple_Thread* reader = new Synchronous_Random_Reader(min_lba, max_lba, seed * 136);
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

Asynch_Random_Workload::Asynch_Random_Workload(double writes_probability)
	: writes_probability(writes_probability) {}

vector<Thread*> Asynch_Random_Workload::generate() {
	//Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	Simple_Thread* thread = new Asynchronous_Random_Reader_Writer(min_lba, max_lba, 2521, writes_probability);
	Individual_Threads_Statistics::init();
	Individual_Threads_Statistics::register_thread(thread, "");
	//init_write->add_follow_up_thread(thread);
	//init_write->set_statistics_gatherer(stats);
	//init_write->set_experiment_thread(true);
	vector<Thread*> threads(1, thread);
	return threads;
}

//*****************************************************************************************
//				Classical INIT workload
//*****************************************************************************************

vector<Thread*> Init_Workload::generate() {
	Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	Simple_Thread* thread = new Asynchronous_Random_Writer(min_lba, max_lba, 23623);
	//Simple_Thread* thread1 = new Asynchronous_Random_Reader(min_lba, max_lba, 2363);
	init_write->add_follow_up_thread(thread);
	//init_write->add_follow_up_thread(thread1);
	vector<Thread*> threads(1, init_write);
	return threads;
}

//*****************************************************************************************
//				Sequentail write to calibrate the SSD
//*****************************************************************************************

vector<Thread*> Init_Write::generate() {
	Simple_Thread* init_write = new Asynchronous_Sequential_Writer(min_lba, max_lba);
	return vector<Thread*>(1, init_write);
}

//*****************************************************************************************
//				SYNCH SEQUENTIAL WRITE
//*****************************************************************************************

vector<Thread*> Synch_Write::generate() {
	int seed = 235325;
	int num_files = INFINITE;
	int max_file_size = 1000;
	Thread* fm = new File_Manager(min_lba, max_lba, num_files, max_file_size, seed * 13);
	vector<Thread*> threads(1, fm);
	return threads;
}

//*****************************************************************************************
//				File System With Noise
//*****************************************************************************************

vector<Thread*> File_System_With_Noise::generate() {

	long log_space_per_thread = max_lba / 4;
	long max_file_size = log_space_per_thread / 8;

	Simple_Thread* seq = new Asynchronous_Sequential_Writer(0, log_space_per_thread * 2);

	Simple_Thread* t1 = new Asynchronous_Random_Writer(0, log_space_per_thread * 2, 35722);
	Simple_Thread* t2 = new Asynchronous_Random_Reader(0, log_space_per_thread * 2, 3456);
	Thread* t3 = new File_Manager(log_space_per_thread * 2 + 1, log_space_per_thread * 4, INFINITE, max_file_size, 713);
	//Thread* t4 = new File_Manager(log_space_per_thread * 3 + 1, log_space_per_thread * 4, INFINITE, max_file_size, 5);

	seq->add_follow_up_thread(t1);
	//seq->add_follow_up_thread(t2);
	seq->add_follow_up_thread(t3);

	vector<Thread*> threads;
	threads.push_back(t3);
	//threads.push_back(t4);

	Individual_Threads_Statistics::init();
	Individual_Threads_Statistics::register_thread(seq, "Asynchronous Sequential Writer Thread");
	Individual_Threads_Statistics::register_thread(t1, "Asynchronous Random Thread Reader");
	Individual_Threads_Statistics::register_thread(t2, "Asynchronous Random Thread Writer");
	Individual_Threads_Statistics::register_thread(t3, "File Manager 1");
	//Individual_Threads_Statistics::register_thread(t4, "File Manager 2");

	return threads;
}

vector<Thread*> Synch_Random_Workload::generate() {
	Simple_Thread* t = new Synchronous_Random_Writer(min_lba, max_lba, 2345);
	return vector<Thread*>(1, t);
}
