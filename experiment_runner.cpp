#define BOOST_FILESYSTEM_VERSION 3
#include "ssd.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>  /* defines FILENAME_MAX */
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir
#include <iostream>
//#include <boost/filesystem.hpp>

#define SIZE 2

using namespace ssd;

const string Experiment_Runner::markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

const bool Experiment_Runner::REMOVE_GLE_SCRIPTS_AGAIN = false;

const double Experiment_Runner::M = 1000000.0; // One million
const double Experiment_Runner::K = 1000.0;    // One thousand

double Experiment_Runner::calibration_precision      = 1.0; // microseconds
double Experiment_Runner::calibration_starting_point = 15.00; // microseconds

string Experiment_Runner::graph_filename_prefix      = "";

void Experiment_Runner::unify_under_one_statistics_gatherer(vector<Thread*> threads, StatisticsGatherer* statistics_gatherer) {
	for (uint i = 0; i < threads.size(); ++i) {
		threads[i]->set_statistics_gatherer(statistics_gatherer);
		unify_under_one_statistics_gatherer(threads[i]->get_follow_up_threads(), statistics_gatherer); // Recurse
	}
}

void Experiment_Runner::run_single_measurment(Workload_Definition* experiment_workload, string name, int IO_limit, OperatingSystem* os) {
	/*experiment_workload->recalculate_lba_range();
	vector<Thread*> init_threads = init_workload->generate_instance();
	os->set_threads(init_threads);
	os->run();

	os->get_ssd()->execute_all_remaining_events();
	save_state(os);
	delete os;
	os = load_state();
	printf("Finished calibration\n");
	StatisticsGatherer::get_global_instance()->init();
	Utilization_Meter::init();*/
	// run experiment workload
	vector<Thread*> experiment_threads = experiment_workload->generate_instance();
	os->set_threads(experiment_threads);
	os->set_num_writes_to_stop_after(IO_limit);
	os->run();

	StatisticsGatherer::get_global_instance()->print();
	//StatisticsGatherer::get_global_instance()->print_gc_info();
	Utilization_Meter::print();
	Individual_Threads_Statistics::print();
	//Free_Space_Meter::print();
	//Free_Space_Per_LUN_Meter::print();
}

vector<ExperimentResult> Experiment_Runner::simple_experiment(Workload_Definition* experiment_workload, string data_folder, string name, long IO_limit, double& variable, double min_val, double max_val, double incr, string calibration_file) {
	assert(experiment_workload != NULL);
	mkdir(data_folder.c_str(), 0755);
	ExperimentResult global_result(name, data_folder, "Global/", "Changing a Var");
	global_result.start_experiment();

	for (variable = min_val; variable <= max_val; variable += incr) {
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("%s :  %f \n", name.c_str(), variable);
		printf("----------------------------------------------------------------------------------------------------------\n");
		OperatingSystem* os = load_state(calibration_file);
		run_single_measurment(experiment_workload, name, IO_limit, os);
		global_result.collect_stats(variable, os->get_experiment_runtime(), StatisticsGatherer::get_global_instance());
		delete os;
	}
	global_result.end_experiment();
	vector<ExperimentResult> results;
	results.push_back(global_result);
	return results;
}

vector<ExperimentResult> Experiment_Runner::simple_experiment(Workload_Definition* experiment_workload, string data_folder, string name, long IO_limit, long& variable, long min_val, long max_val, long incr, string calibration_file) {
	assert(experiment_workload != NULL);
	mkdir(data_folder.c_str(), 0755);
	ExperimentResult global_result(name, data_folder, "Global/", "Changing a Var");
	global_result.start_experiment();

	for (variable = min_val; variable <= max_val; variable += incr) {
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("%s :  %d \n", name.c_str(), variable);
		printf("----------------------------------------------------------------------------------------------------------\n");

		OperatingSystem* os = load_state(calibration_file);
		run_single_measurment(experiment_workload, name, IO_limit, os);
		global_result.collect_stats(variable, os->get_experiment_runtime(), StatisticsGatherer::get_global_instance());
		delete os;
	}
	global_result.end_experiment();
	vector<ExperimentResult> results;
	results.push_back(global_result);
	return results;
}

vector<ExperimentResult> Experiment_Runner::random_writes_on_the_side_experiment(Workload_Definition* workload, int write_threads_min, int write_threads_max, int write_threads_inc, string data_folder, string name, int IO_limit, double used_space, int random_writes_min_lba, int random_writes_max_lba) {
	mkdir(data_folder.c_str(), 0755);
	ExperimentResult global_result       (name, data_folder, "Global/",             "Number of concurrent random write threads");
    ExperimentResult experiment_result   (name, data_folder, "Experiment_Threads/", "Number of concurrent random write threads");
    ExperimentResult write_threads_result(name, data_folder, "Noise_Threads/",      "Number of concurrent random write threads");

    global_result.start_experiment();
    experiment_result.start_experiment();
    write_threads_result.start_experiment();

    for (int random_write_threads = write_threads_min; random_write_threads <= write_threads_max; random_write_threads += write_threads_inc) {
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("%s : Experiment with max %d concurrent random writes threads.\n", name.c_str(), random_write_threads);
		printf("----------------------------------------------------------------------------------------------------------\n");

		StatisticsGatherer* experiment_statistics_gatherer = new StatisticsGatherer();
		StatisticsGatherer* random_writes_statics_gatherer = new StatisticsGatherer();
		Thread* initial_write    = new Asynchronous_Sequential_Writer(0, used_space);
		if (workload != NULL) {
			vector<Thread*> experiment_threads = workload->generate_instance();
			unify_under_one_statistics_gatherer(experiment_threads, experiment_statistics_gatherer);
			initial_write->add_follow_up_threads(experiment_threads);
		}
		for (int i = 0; i < random_write_threads; i++) {
			ulong randseed = (i*3)+537;
			Simple_Thread* random_writes = new Synchronous_Random_Writer(random_writes_min_lba, random_writes_max_lba, randseed);
			Simple_Thread* random_reads = new Synchronous_Random_Reader(random_writes_min_lba, random_writes_max_lba, randseed+461);
			if (workload == NULL) {
				random_writes->set_experiment_thread(true);
				random_reads->set_experiment_thread(true);
			}
			random_writes->set_num_ios(INFINITE);
			random_reads->set_num_ios(INFINITE);
			random_writes->set_statistics_gatherer(random_writes_statics_gatherer);
			random_reads->set_statistics_gatherer(random_writes_statics_gatherer);
			initial_write->add_follow_up_thread(random_writes);
			initial_write->add_follow_up_thread(random_reads);
		}

		vector<Thread*> threads;
		threads.push_back(initial_write);
		OperatingSystem* os = new OperatingSystem();
		os->set_threads(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		os->run();

			// Collect statistics from this experiment iteration (save in csv files)
		global_result.collect_stats       (random_write_threads, os->get_experiment_runtime(), StatisticsGatherer::get_global_instance());
		experiment_result.collect_stats   (random_write_threads, os->get_experiment_runtime(), experiment_statistics_gatherer);
		write_threads_result.collect_stats(random_write_threads, os->get_experiment_runtime(), random_writes_statics_gatherer);

		if (workload == NULL) {
			StatisticsGatherer::get_global_instance()->print();
		} else {
			experiment_statistics_gatherer->print();
		}

		//StatisticsGatherer::get_global_instance()->print();
		//random_writes_statics_gatherer->print();
		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}
		delete os;
		delete experiment_statistics_gatherer;
		delete random_writes_statics_gatherer;
	}
    global_result.end_experiment();
    experiment_result.end_experiment();
    write_threads_result.end_experiment();

    vector<ExperimentResult> results;
    results.push_back(global_result);
    results.push_back(experiment_result);
    results.push_back(write_threads_result);
    if (workload != NULL)
    	delete workload;
    return results;
}

ExperimentResult Experiment_Runner::copyback_experiment(vector<Thread*> (*experiment)(int highest_lba), int used_space, int max_copybacks, string data_folder, string name, int IO_limit) {
    ExperimentResult experiment_result(name, data_folder, "", "CopyBacks allowed before ECC check");
    experiment_result.start_experiment();

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
    for (int copybacks_allowed = 0; copybacks_allowed <= max_copybacks; copybacks_allowed += 1) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("---------------------------------------\n");
		printf("Experiment with %d copybacks allowed.\n", copybacks_allowed);
		printf("---------------------------------------\n");

		MAX_REPEATED_COPY_BACKS_ALLOWED = copybacks_allowed;

		// Run experiment
		vector<Thread*> threads = experiment(highest_lba);
		OperatingSystem* os = new OperatingSystem();
		os->set_threads(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		os->run();

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(copybacks_allowed, os->get_experiment_runtime());

		StatisticsGatherer::get_global_instance()->print();
		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	experiment_result.end_experiment();
	return experiment_result;
}

ExperimentResult Experiment_Runner::copyback_map_experiment(vector<Thread*> (*experiment)(int highest_lba), int cb_map_min, int cb_map_max, int cb_map_inc, int used_space, string data_folder, string name, int IO_limit) {
    ExperimentResult experiment_result(name, data_folder, "", "Max copyback map size");
    experiment_result.start_experiment();

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
    for (int copyback_map_size = cb_map_min; copyback_map_size <= cb_map_max; copyback_map_size += cb_map_inc) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("-------------------------------------------------------\n");
		printf("Experiment with %d copybacks allowed in copyback map.  \n", copyback_map_size);
		printf("-------------------------------------------------------\n");

		MAX_ITEMS_IN_COPY_BACK_MAP = copyback_map_size;

		// Run experiment
		vector<Thread*> threads = experiment(highest_lba);
		OperatingSystem* os = new OperatingSystem();
		os->set_threads(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		os->run();

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(copyback_map_size, os->get_experiment_runtime());

		StatisticsGatherer::get_global_instance()->print();
		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	experiment_result.end_experiment();
	return experiment_result;
}

// currently, this method checks if there if a file already exists, and if so, assumes it is valid.
// ideally, a check should be made to ensure the saved SSD state matches with the state of the current global parameters
void Experiment_Runner::calibrate_and_save(string file_name, Workload_Definition* workload) {
	std::ifstream ifile(file_name.c_str());
	if (ifile) {
		return; // file exists
	}
	printf("Creating calibrated SSD state.\n");
	OperatingSystem* os = new OperatingSystem();
	vector<Thread*> init_threads = workload->generate_instance();
	os->set_threads(init_threads);
	os->run();
	os->get_ssd()->execute_all_remaining_events();
	save_state(os, file_name);
	delete os;
}

void Experiment_Runner::save_state(OperatingSystem* os, string file_name) {
	std::ofstream file(file_name.c_str());
	boost::archive::text_oarchive oa(file);
	oa.register_type<FtlImpl_Page>( );
	oa.register_type<Block_manager_parallel>( );
	oa << os;
}

OperatingSystem* Experiment_Runner::load_state(string file_name) {
	std::ifstream file(file_name.c_str());
	boost::archive::text_iarchive ia(file);
	ia.register_type<FtlImpl_Page>( );
	ia.register_type<Block_manager_parallel>();
	OperatingSystem* os;
	ia >> os;
	os->get_ssd()->get_scheduler()->init();
	return os;
}
