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

double r1 = .1; // Relation 1 percentage use of addresses
double r2 = .2; // Relation 2 percentage use of addresses
double ns = .3; // "noise space" percentage use of addresses
double fs = .4; // Free space percentage use of addresses
//----------
// total  =1.0

vector<Thread*> grace_hash_join(int highest_lba, bool use_flexible_reads, bool use_tagging) {
	Thread* initial_write    = new Asynchronous_Sequential_Thread(0, highest_lba*(r1+r2+ns), 1, WRITE, 1, 1);
	Thread* grace_hash_join  = new Grace_Hash_Join(0,highest_lba*r1, highest_lba*r1+1, highest_lba*(r1+r2), highest_lba*(r1+r2+ns)+1,highest_lba*(r1+r2+ns+fs), highest_lba*(r1+r2)/2,1,use_flexible_reads,use_tagging,32);
	Thread* background_noise = new Asynchronous_Random_Thread_Reader_Writer(highest_lba*(r1+r2)+1, highest_lba*(r1+r2+ns),10,0,1);

	grace_hash_join->set_experiment_thread(true);
//	background_noise->set_experiment_thread(true);

	initial_write->add_follow_up_thread(grace_hash_join);
	initial_write->add_follow_up_thread(background_noise);

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

	int IO_limit = 100000;
	int space_min = 40;
	int space_max = 80;
	int space_inc = 5;

	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 15;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

	double start_time = Experiment_Runner::wall_clock_time();

	vector<ExperimentResult> exp;

	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join,			space_min, space_max, space_inc, exp_folder + "__/", "Both off", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_flex,	    space_min, space_max, space_inc, exp_folder + "F_/", "Flexible reads enabled", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_tag,	    space_min, space_max, space_inc, exp_folder + "_T/", "Tagging enabled", IO_limit) );
	exp.push_back( Experiment_Runner::overprovisioning_experiment(grace_hash_join_flex_tag, space_min, space_max, space_inc, exp_folder + "FT/", "Flexible reads and tagging enabled", IO_limit) );

	uint mean_pos_in_datafile = std::find(exp[0].column_names.begin(), exp[0].column_names.end(), "Write latency, mean (µs)") - exp[0].column_names.begin();
	assert(mean_pos_in_datafile != exp[0].column_names.size());

	vector<int> used_space_values_to_show;
	for (int i = space_min; i <= space_max; i += space_inc)
		used_space_values_to_show.push_back(i); // Show all used spaces values in multi-graphs

	int sx = 16;
	int sy = 8;

	//for (int i = 0; i < exp[0].column_names.size(); i++) printf("%d: %s\n", i, exp[0].column_names[i].c_str());

	chdir(exp_folder.c_str());

	Experiment_Runner::graph(sx, sy,   "Throughput", 				"throughput", 			24, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Write Throughput", 			"throughput_write", 	25, exp, 30);
	Experiment_Runner::graph(sx, sy,   "Num Erases", 				"num_erases", 			8, 	exp, 16000);
	Experiment_Runner::graph(sx, sy,   "Num Migrations", 			"num_migrations", 		3, 	exp, 500000);

	Experiment_Runner::graph(sx, sy,   "Write latency, mean", 			"Write latency, mean", 		9, 	exp, 12000);
	Experiment_Runner::graph(sx, sy,   "Write latency, max", 			"Write latency, max", 		14, exp, 40000);
	Experiment_Runner::graph(sx, sy,   "Write latency, std", 			"Write latency, std", 		15, exp, 14000);

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

