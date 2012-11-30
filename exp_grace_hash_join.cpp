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


#include "ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

//double r1 = .1; // Relation 1 percentage use of addresses
//double r2 = .2; // Relation 2 percentage use of addresses

// Internal division of space in each grace hash join instance
double r1 = .20; // Relation 1 percentage use of addresses
double r2 = .20; // Relation 2 percentage use of addresses
double fs = .60; // Free space percentage use of addresses

// Overall division of space
double gh = .5; // Grace hash join(s) percentage use of addresses
double ns = .5; // "Noise space" percentage use of addresses
//------------;
// total  =1.0

/* TO-DO stuff
 * - Variable grace threads
 * - Experiment param =  #write threads, secondly, #grace threads
 * - GC in stats (again)
 * - Attach statistics gatherer(s) to threads, instead of the opposite
 * - Scheduling?
 * - Vagrind
 */

Thread* grace_hash_join_thread(int lowest_lba, int highest_lba, bool use_flexible_reads, bool use_tagging) {
	int span = highest_lba - lowest_lba;
	assert(span >= 10);
	return new Grace_Hash_Join(lowest_lba,                lowest_lba+span*r1,
			                   lowest_lba+span*r1+1,      lowest_lba+span*(r1+r2),
			                   lowest_lba+span*(r1+r2)+1, lowest_lba+span*(r1+r2+fs),
			                   (span*(r1+r2))/2, 1, use_flexible_reads, use_tagging, 32);
}

vector<Thread*> grace_hash_join(int highest_lba, bool use_flexible_reads, bool use_tagging, int random_read_threads = 0, int random_write_threads = 6, int grace_hash_join_threads = 1) {
/*	printf("Address division overview:\n");
	printf("Address 0 - %f: Relation one\n", highest_lba*r1);
	printf("Address %f - %f: Relation two\n", (int)highest_lba*r1+1, (int)highest_lba*(r1+r2));
	printf("Address %f - %f: Noise space\n", (int)highest_lba*(r1+r2)+1, (int)highest_lba*(r1+r2+ns));
	printf("Address %f - %f: Free space\n", (int)highest_lba*(r1+r2+ns), (int)highest_lba);*/
/*	int random_read_threads     = 0;
	int random_write_threads    = 6;
	int grace_hash_join_threads = 2; */

	//int noise_repetition = std::numeric_limits<int>::max();
	Thread* initial_write    = new Asynchronous_Sequential_Thread(0, highest_lba, 1, WRITE, 1, 1);
	//Thread* background_noise = new Asynchronous_Random_Thread_Reader_Writer(highest_lba*gh+1, highest_lba*(gh+ns),noise_repetition);
	//Thread* background_relation_reads = new Asynchronous_Random_Thread(0,highest_lba*(gh), 10,666,READ,noise_timebreaks,1);

/*	for (int i = 0; i < random_write_threads; i++) {
		Thread* random_writes = new Synchronous_Random_Thread(highest_lba*gh+1, highest_lba*(gh+ns), std::numeric_limits<int>::max(), i+537, WRITE, 999);
		//random_writes->set_experiment_thread(true);
		initial_write->add_follow_up_thread(random_writes);
	}*/

	// TODO: For giving the grace hash join threads chucks of space of uneaven sizes
	vector<double> space_fractions;

	for (int i = 0; i < random_read_threads; i++) {
		Thread* random_reads = new Synchronous_Random_Thread(highest_lba*gh+1 + 0, highest_lba*(gh+ns), std::numeric_limits<int>::max(), i+637, READ);
		//random_reads->set_experiment_thread(true);
		initial_write->add_follow_up_thread(random_reads);
	}

	//	background_noise->set_experiment_thread(true);
	// initial_write->add_follow_up_thread(background_noise);
	// initial_write->add_follow_up_thread(background_relation_reads);

//	printf("%d\n",highest_lba);
//	printf("%d <-> %d\n", 0, (int) (highest_lba*gh));

	for (int gt = 0; gt < grace_hash_join_threads; gt++) {
		Thread* recursive_madness = initial_write;
		int low_lba =  (highest_lba*gh)*gt/grace_hash_join_threads;
		int high_lba = ((highest_lba*gh)*(gt+1)/grace_hash_join_threads)-1;
//		printf("(%d -> %d)\n", low_lba, high_lba);
		for (int i = 0; i < 1000; i++) {
			//Thread* grace_hash_join = new Grace_Hash_Join(0,highest_lba*r1, highest_lba*r1+1, highest_lba*(r1+r2), highest_lba*(r1+r2+ns)+1,highest_lba*(r1+r2+ns+fs), highest_lba*(r1+r2)/2,1,use_flexible_reads,use_tagging,32);
			//printf("%d -> %d\n", (int) (highest_lba*gh)*gt/grace_hash_join_threads, (int) ((highest_lba*gh)*(gt+1)/grace_hash_join_threads)-1);
			Thread* grace_hash_join = grace_hash_join_thread(low_lba, high_lba, use_flexible_reads, use_tagging);
			grace_hash_join->set_experiment_thread(true);
			recursive_madness->add_follow_up_thread(grace_hash_join);
			recursive_madness = grace_hash_join;
		}
	}
	vector<Thread*> threads;
	threads.push_back(initial_write);

	return threads;
}

vector<Thread*> grace_hash_join(int highest_lba, double IO_submission_rate) {
	return grace_hash_join(highest_lba, false, false);
}

vector<Thread*> grace_hash_join_flex(int highest_lba, double IO_submission_rate) {
	return grace_hash_join(highest_lba, true, false);
}

vector<Thread*> grace_hash_join_tag(int highest_lba, double IO_submission_rate) {
	return grace_hash_join(highest_lba, false, true);
}

vector<Thread*> grace_hash_join_flex_tag(int highest_lba, double IO_submission_rate) {
	return grace_hash_join(highest_lba, true, true);
}

int main()
{
	string exp_folder  = "exp_grace_hash_join/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 100;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 250000;
	int space_min = 40;
	int space_max = 85;
	int space_inc = 5;

	int write_threads_min = 0;
	int write_threads_max = 6;
	double used_space = .80; // overprovisioning level for variable random write threads experiment

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 15;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

	const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
	const int avail_pages = num_pages * used_space;

	double start_time = Experiment_Runner::wall_clock_time();

	vector<ExperimentResult> exp;

	exp.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join,			 write_threads_min, write_threads_max, 1, exp_folder + "__/", "None", IO_limit, used_space, avail_pages*ns+1, avail_pages) );
	exp.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_flex,	 write_threads_min, write_threads_max, 1, exp_folder + "F_/", "Flexible reads", IO_limit, used_space, avail_pages*ns+1, avail_pages) );
	exp.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_tag,		 write_threads_min, write_threads_max, 1, exp_folder + "_T/", "Tagging", IO_limit, used_space, avail_pages*ns+1, avail_pages) );
	exp.push_back( Experiment_Runner::random_writes_on_the_side_experiment(grace_hash_join_flex_tag, write_threads_min, write_threads_max, 1, exp_folder + "FT/", "Flexible reads + tagging", IO_limit, used_space, avail_pages*ns+1, avail_pages) );

/*
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join,			space_min, space_max, space_inc, exp_folder + "__/", "None", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_flex,	    space_min, space_max, space_inc, exp_folder + "F_/", "Flexible reads", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_tag,	    space_min, space_max, space_inc, exp_folder + "_T/", "Tagging", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_flex_tag, space_min, space_max, space_inc, exp_folder + "FT/", "Flexible reads + tagging", IO_limit) );
*/
	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write latency, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	for (int i = 0; i < exp[0].column_names.size(); i++) printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	chdir(exp_folder.c_str());

	Experiment_Runner::graph(sx, sy,   "Throughput", 				"throughput", 			24, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Write Throughput", 			"throughput_write", 	25, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Read Throughput", 			"throughput_read", 		26, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Num Erases", 				"num_erases", 			8, 	exp, 16000);
	Experiment_Runner::graph(sx, sy,   "Num Migrations", 			"num_migrations", 		3, 	exp, 500000);

	Experiment_Runner::graph(sx, sy,   "Write latency, mean", 			"Write latency, mean", 		9, 	exp, 1000);
	Experiment_Runner::graph(sx, sy,   "Write latency, max", 			"Write latency, max", 		14, exp, 10000);
	Experiment_Runner::graph(sx, sy,   "Write latency, std", 			"Write latency, std", 		15, exp, 1000);

	Experiment_Runner::graph(sx, sy,   "Read latency, mean", 			"Read latency, mean", 		16,	exp, 1000);
	Experiment_Runner::graph(sx, sy,   "Read latency, max", 			"Read latency, max", 		21, exp, 10000);
	Experiment_Runner::graph(sx, sy,   "Read latency, std", 			"Read latency, std", 		22, exp, 1000);

	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 90", exp, 90, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 80", exp, 80, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 70", exp, 70, 1, 4);
	Experiment_Runner::cross_experiment_waittime_histogram(sx, sy/2, "waittime_histogram 70", exp, 60, 1, 4);

	for (uint i = 0; i < exp.size(); i++) {
		printf("%s\n", exp[i].data_folder.c_str());
		chdir(exp[i].data_folder.c_str());
		Experiment_Runner::waittime_boxplot  		(sx, sy,   "Write latency boxplot", "boxplot", mean_pos_in_datafile, exp[i]);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, 1, 4);
		Experiment_Runner::waittime_histogram		(sx, sy/2, "waittime-histograms-allIOs", exp[i], used_space_values_to_show, true);
		Experiment_Runner::age_histogram			(sx, sy/2, "age-histograms", exp[i], used_space_values_to_show);
		Experiment_Runner::queue_length_history		(sx, sy/2, "queue_length", exp[i], used_space_values_to_show);
		Experiment_Runner::throughput_history		(sx, sy/2, "throughput_history", exp[i], used_space_values_to_show);
	}

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	return 0;

/*
	string exp_folder  = "exp_sequential/";
	mkdir(exp_folder.c_str(), 0755);

	load_config();

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	PAGE_READ_DELAY = 50;
	PAGE_WRITE_DELAY = 200;
	BUS_CTRL_DELAY = 5;
	BUS_DATA_DELAY = 100;
	BLOCK_ERASE_DELAY = 1500;

	int IO_limit = 100000;

	PRINT_LEVEL = 1;
	MAX_SSD_QUEUE_SIZE = 15;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

	double start_time = Experiment_Runner::wall_clock_time();

	// Run experiment
	vector<Thread*> threads = grace_hash_join(1000,false,true);
	OperatingSystem* os = new OperatingSystem(threads);
	os->set_num_writes_to_stop_after(IO_limit);
	try {
		os->run();
	} catch(...) {
		printf("An exception was thrown, but we continue for now\n");
	}


	// Print shit
	StatisticsGatherer::get_global_instance()->print();
	//if (PRINT_LEVEL >= 1) {
		StateVisualiser::print_page_status();
		StateVisualiser::print_block_ages();
	//}

	delete os;

	double end_time = Experiment_Runner::wall_clock_time();
	printf("=== Entire experiment finished in %s ===\n", Experiment_Runner::pretty_time(end_time - start_time).c_str());

	return 0;
*/
}

