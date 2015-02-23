#define BOOST_FILESYSTEM_VERSION 3
#include "ssd.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>  /* defines FILENAME_MAX */
#include <iostream>

#define SIZE 2

using namespace ssd;

const string Experiment::markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

const bool Experiment::REMOVE_GLE_SCRIPTS_AGAIN = false;

const double Experiment::M = 1000000.0; // One million
const double Experiment::K = 1000.0;    // One thousand

double Experiment::calibration_precision      = 1.0; // microseconds
double Experiment::calibration_starting_point = 15.00; // microseconds
string Experiment::base_folder = get_current_dir_name();

Experiment::Experiment()
	: d_variable(NULL), d_min(0), d_max(0), d_incr(0),
	  i_variable(NULL), i_min(0), i_max(0), i_incr(0),
	  io_limit(NUMBER_OF_ADDRESSABLE_PAGES()),
	  workload(NULL), calibration_workload(NULL),
	  calibrate_for_each_point(false),
	  results(),
	  generate_trace_file(false),
	  alternate_location_for_results_file("")
{}

void Experiment::unify_under_one_statistics_gatherer(vector<Thread*> threads, StatisticsGatherer* statistics_gatherer) {
	for (uint i = 0; i < threads.size(); ++i) {
		threads[i]->set_statistics_gatherer(statistics_gatherer);
		unify_under_one_statistics_gatherer(threads[i]->get_follow_up_threads(), statistics_gatherer); // Recurse
	}
}

void Experiment::set_variable(double* variable, double low, double high, double incr, string name) {
	d_variable = variable;
	d_min = low;
	d_max = high;
	d_incr = incr;
	variable_name = name;
}

void Experiment::set_variable(int* variable, int low, int high, int incr, string name) {
	i_variable = variable;
	i_min = low;
	i_max = high;
	i_incr = incr;
	variable_name = name;
}

void Experiment::run(string name) {
	Thread::set_record_internal_statistics(true);
	StatisticsGatherer::set_record_statistics(true);

	if (MAX_REPEATED_COPY_BACKS_ALLOWED > 0) {
		fprintf(stderr, "The simulation parameter MAX_REPEATED_COPY_BACKS_ALLOWED is greater than 0. This is still buggy, and so we fail.\nSet this parameter to 0 to remove this error message.\n");
	}

	if (i_variable == NULL && d_variable == NULL) {
		run_single_point(name);
	}
	else if (d_variable != NULL) {
		simple_experiment_double(name, d_variable, d_min, d_max, d_incr);
	}
	else if (i_variable != NULL) {
		simple_experiment_double(name, i_variable, i_min, i_max, i_incr);
	}
}

void Experiment::run_single_point(string name) {
	string data_folder = base_folder + name + "/";
	mkdir(data_folder.c_str(), 0755);
	StatisticsGatherer::set_record_statistics(true);
	Thread::set_record_internal_statistics(true);
	Experiment_Result global_result(name, data_folder, "Global/", "");
	Individual_Threads_Statistics::init();
	global_result.start_experiment();
	Free_Space_Meter::init();
	Free_Space_Per_LUN_Meter::init();

	if (generate_trace_file) {
		VisualTracer::init(data_folder);
	} else {
		VisualTracer::init();
	}

	write_config_file(data_folder);
	Queue_Length_Statistics::init();

	printf("calibration_file : %s\n", calibration_file.c_str());

	OperatingSystem* os = calibration_file.empty() ? new OperatingSystem() : load_state(calibration_file);
	//os->set_progress_meter_granularity(10);
	if (workload != NULL) {
		vector<Thread*> experiment_threads = workload->generate_instance();
		os->set_threads(experiment_threads);
	}
	os->set_num_writes_to_stop_after(io_limit);
	os->run();

	StatisticsGatherer::get_global_instance()->print();
	StatisticsGatherer::get_global_instance()->print_mapping_info();
	StatisticsGatherer::get_global_instance()->print_gc_info();
	Utilization_Meter::print();
	//Individual_Threads_Statistics::print();
	//Queue_Length_Statistics::print_distribution();
	//Queue_Length_Statistics::print_avg();
	Free_Space_Meter::print();
	Free_Space_Per_LUN_Meter::print();

	global_result.collect_stats("0", StatisticsGatherer::get_global_instance());
	write_results_file(data_folder);
	if (!alternate_location_for_results_file.compare("") == 0) {
		printf("writing results in %s\n", alternate_location_for_results_file.c_str());
		write_results_file(alternate_location_for_results_file);
	}

	global_result.end_experiment();
	vector<Experiment_Result> result;
	result.push_back(global_result);
	results.push_back(result);
	delete os;
}

template <class T>
void Experiment::simple_experiment_double(string name, T* var, T min, T max, T inc) {
	string data_folder = base_folder + name + "/";
	mkdir(data_folder.c_str(), 0755);
	Experiment_Result global_result(name, data_folder, "Global/", variable_name);
	global_result.start_experiment();
	T& variable = *var;
	for (variable = min; variable <= max; ) {
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("%s :  %s \n", name.c_str(), to_string(variable).c_str());
		printf("----------------------------------------------------------------------------------------------------------\n");

		string point_folder_name = data_folder + to_string(variable) + "/";
		mkdir(point_folder_name.c_str(), 0755);
		if (generate_trace_file) {
			VisualTracer::init(data_folder);
		} else {
			VisualTracer::init();
		}
		write_config_file(point_folder_name);
		Queue_Length_Statistics::init();
		Free_Space_Meter::init();
		Free_Space_Per_LUN_Meter::init();

		OperatingSystem* os;
		if (calibrate_for_each_point && calibration_workload != NULL) {
			string calib_file_name = "calib-" + name + "-" + to_string(variable) + ".txt";
			Experiment::calibrate_and_save(calibration_workload, calib_file_name, NUMBER_OF_ADDRESSABLE_PAGES() * 8);
			os = load_state(calib_file_name);
			//StateVisualiser::print_page_status();
		} else if (!calibration_file.empty()) {
			os = load_state(calibration_file);
		} else {
			os = new OperatingSystem();
		}

		if (workload != NULL) {
			vector<Thread*> experiment_threads = workload->generate_instance();
			os->set_threads(experiment_threads);
		}
		StatisticsGatherer::set_record_statistics(true);
		os->set_num_writes_to_stop_after(io_limit);
		os->run();
		StatisticsGatherer::get_global_instance()->print();
		//StatisticsGatherer::get_global_instance()->print_gc_info();
		//Utilization_Meter::print();
		//Queue_Length_Statistics::print_avg();
		//Free_Space_Meter::print();
		//Free_Space_Per_LUN_Meter::print();
		stringstream var_str;
		var_str << variable;
		global_result.collect_stats(var_str.str(), StatisticsGatherer::get_global_instance());
		StatisticData::init();
		write_results_file(point_folder_name);
		delete os;

		if (exponential_increase) {
			variable *= inc;
		}
		else {
			variable += inc;
		}
	}
	global_result.end_experiment();
	vector<Experiment_Result> result;
	result.push_back(global_result);
	results.push_back(result);
}

vector<Experiment_Result> Experiment::random_writes_on_the_side_experiment(Workload_Definition* workload, int write_threads_min, int write_threads_max, int write_threads_inc, string name, int IO_limit, double used_space, int random_writes_min_lba, int random_writes_max_lba) {
	string data_folder = base_folder + name;
	mkdir(data_folder.c_str(), 0755);
	Experiment_Result global_result       (name, data_folder, "Global/",             "Number of concurrent random write threads");
    Experiment_Result experiment_result   (name, data_folder, "Experiment_Threads/", "Number of concurrent random write threads");
    Experiment_Result write_threads_result(name, data_folder, "Noise_Threads/",      "Number of concurrent random write threads");

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
			/*if (workload == NULL) {
				random_writes->set_experiment_thread(true);
				random_reads->set_experiment_thread(true);
			}*/
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

		stringstream var_str;
		var_str << random_write_threads;

			// Collect statistics from this experiment iteration (save in csv files)
		global_result.collect_stats       (var_str.str(), StatisticsGatherer::get_global_instance());
		experiment_result.collect_stats   (var_str.str(), experiment_statistics_gatherer);
		write_threads_result.collect_stats(var_str.str(), random_writes_statics_gatherer);

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

    vector<Experiment_Result> results;
    results.push_back(global_result);
    results.push_back(experiment_result);
    results.push_back(write_threads_result);
    if (workload != NULL)
    	delete workload;
    return results;
}

Experiment_Result Experiment::copyback_experiment(vector<Thread*> (*experiment)(int highest_lba), int used_space, int max_copybacks, string data_folder, string name, int IO_limit) {
    Experiment_Result experiment_result(name, data_folder, "", "CopyBacks allowed before ECC check");
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

		stringstream var_str;
		var_str << copybacks_allowed;

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(var_str.str());

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

Experiment_Result Experiment::copyback_map_experiment(vector<Thread*> (*experiment)(int highest_lba), int cb_map_min, int cb_map_max, int cb_map_inc, int used_space, string data_folder, string name, int IO_limit) {
    Experiment_Result experiment_result(name, data_folder, "", "Max copyback map size");
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

		stringstream var_str;
		var_str << copyback_map_size;

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(var_str.str());

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
void Experiment::calibrate_and_save(Workload_Definition* workload, string name, int num_IOs, bool force) {
	//string file_name = base_folder + "calibrated_state.txt";
	string file_name = base_folder + name;
	std::ifstream ifile(file_name.c_str());
	if (ifile && !force) {
		return; // file exists
	}
	StatisticsGatherer::set_record_statistics(false);
	//StatisticsGatherer::get_global_instance()->init();
	Thread::set_record_internal_statistics(false);
	VisualTracer::init();
	//Free_Space_Meter::init();
	//Free_Space_Per_LUN_Meter::init();
	printf("Creating calibrated SSD state.\n");
	OperatingSystem* os = new OperatingSystem();
	//num_IOs /= 2;
	os->set_num_writes_to_stop_after(num_IOs);
	vector<Thread*> init_threads = workload->generate_instance();
	os->set_threads(init_threads);
	os->set_progress_meter_granularity(1000);

	os->run();
	os->get_ssd()->execute_all_remaining_events();

	//Block_Manager_Tag_Groups* bm = (Block_Manager_Tag_Groups*) os->get_ssd()->get_scheduler()->get_bm();
	//bm->print();

	save_state(os, file_name);
	//StateVisualiser::print_page_status();
	//StatisticsGatherer::get_global_instance()->print();
	//Free_Space_Meter::print();
	//Free_Space_Per_LUN_Meter::print();
	delete os;
}

void Experiment::write_config_file(string folder_name) {
	string file_name = folder_name + "configuration.txt";
	FILE* file = fopen(file_name.c_str() , "w");
	print_config(file);
	fclose(file);
}

void Experiment::write_results_file(string folder_name) {
	string file_name = folder_name + "results.txt";
	printf("Writing results file in:   %s\n", file_name.c_str());
	FILE* file = fopen(file_name.c_str() , "w");
	StatisticsGatherer::get_global_instance()->print_simple(file);
	double channel_util = Utilization_Meter::get_avg_channel_utilization();
	fprintf(file, "channel util:\t%f\n", channel_util);
	double LUN_util = Utilization_Meter::get_avg_LUN_utilization();
	fprintf(file, "LUN util:\t%f\n\n", LUN_util);

	for (int i = 0; i < SSD_SIZE; i++) {
		fprintf(file, "Channel util for package %d:\t%f\n", i, Utilization_Meter::get_channel_utilization(i) );
	}
	fprintf(file, "\n");
	for (int i = 0; i < SSD_SIZE * PACKAGE_SIZE; i++) {
		fprintf(file, "LUN util for LUN %d:\t%f\n", i, Utilization_Meter::get_LUN_utilization(i) );
	}
	fprintf(file, "\n");

	//Individual_Threads_Statistics::print();
	for (int i = 0; i < Individual_Threads_Statistics::size(); i++) {
		StatisticsGatherer* sg = Individual_Threads_Statistics::get_stats_for_thread(i);
		if (sg != NULL) {
			int num_reads = sg->total_reads();
			int num_writes = sg->total_writes();
			fprintf(file, "Thread reads %d: %d\n", i, num_reads);
			fprintf(file, "Thread writes %d: %d\n", i, num_writes);
		}
	}

	fclose(file);
}

void Experiment::save_state(OperatingSystem* os, string file_name) {
	vector<Thread*> threads = os->get_non_finished_threads();
	std::ofstream file(file_name.c_str());
	printf("%s\n", file_name.c_str());
	boost::archive::text_oarchive oa(file);
	oa.register_type<FtlImpl_Page>( );
	oa.register_type<FAST>( );
	oa.register_type<DFTL>( );
	oa.register_type<Block_manager_parallel>( );
	oa.register_type<Sequential_Locality_BM>( );
	oa.register_type<Block_Manager_Tag_Groups>( );
	oa.register_type<File_Manager>( );
	oa.register_type<Simple_Thread>( );
	oa.register_type<Random_IO_Pattern>( );
	oa.register_type<Sequential_IO_Pattern>( );
	oa.register_type<WRITES>( );
	oa.register_type<TRIMS>( );
	oa.register_type<READS>( );
	oa.register_type<READS_OR_WRITES>();
	oa.register_type<Asynchronous_Random_Writer>();
	oa.register_type<Asynchronous_Random_Reader>();
	oa.register_type<Synchronous_Random_Writer>( );
	oa.register_type<MTRand>();
	oa.register_type<MTRand_closed>();
	oa.register_type<MTRand_open>();
	oa.register_type<MTRand53>();
	oa.register_type<Garbage_Collector_Greedy>();
	//oa.register_type<Garbage_Collector_LRU>();
	oa << os;
	oa << threads;
	file.close();
}

OperatingSystem* Experiment::load_state(string name) {
	string file_name = base_folder + name;
	printf("loading calibration file:  %s\n", file_name.c_str());
	std::ifstream file(file_name.c_str());
	boost::archive::text_iarchive ia(file);
	ia.register_type<FtlImpl_Page>( );
	ia.register_type<FAST>();
	ia.register_type<DFTL>();
	ia.register_type<Block_manager_parallel>();
	ia.register_type<Sequential_Locality_BM>( );
	ia.register_type<File_Manager>( );
	ia.register_type<Simple_Thread>( );
	ia.register_type<Random_IO_Pattern>( );
	ia.register_type<Sequential_IO_Pattern>( );
	ia.register_type<WRITES>( );
	ia.register_type<TRIMS>( );
	ia.register_type<READS>( );
	ia.register_type<READS_OR_WRITES>();
	ia.register_type<Asynchronous_Random_Writer>();
	ia.register_type<Asynchronous_Random_Reader>();
	ia.register_type<Synchronous_Random_Writer>( );
	ia.register_type<MTRand>();
	ia.register_type<MTRand_closed>();
	ia.register_type<MTRand_open>();
	ia.register_type<MTRand53>();
	ia.register_type<Garbage_Collector_Greedy>();
	//ia.register_type<Garbage_Collector_LRU>();
	OperatingSystem* os;
	ia >> os;
	vector<Thread*> threads;
	ia >> threads;
	Individual_Threads_Statistics::init();
	for (auto t : threads) {
		//Individual_Threads_Statistics::register_thread(t, "");
	}
	os->set_threads(threads);
	//os->init_threads();
	IOScheduler* scheduler = os->get_ssd()->get_scheduler();
	scheduler->init();
	Block_manager_parent* bm = Block_manager_parent::get_new_instance();
	bm->copy_state(scheduler->get_bm());
	delete scheduler->get_bm();

	scheduler->set_block_manager(bm);
	FtlParent* ftl = os->get_ssd()->get_ftl();
	//delete ftl->get_block_manager();
	ftl->set_block_manager(bm);
	Migrator* m = scheduler->get_migrator();
	m->set_block_manager(bm);
	Garbage_Collector* gc = m->get_garbage_collector();
	gc->set_block_manager(bm);

	return os;
}

void Experiment::create_base_folder(string name) {
	string exp_folder = get_current_dir_name() + name;
	printf("creating exp folder:  %s\n", get_current_dir_name());
	base_folder = exp_folder;
	mkdir(base_folder.c_str(), 0755);
}
