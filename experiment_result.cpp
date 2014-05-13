#include <sstream>
#include <sys/stat.h> // mkdir
#include "ssd.h"
using namespace ssd;

const string Experiment_Result::throughput_column_name		= "Average throughput (IOs/s)"; // e.g. "Average throughput (IOs/s)". Becomes y-axis on aggregated (for all experiments with different values for the variable parameter) throughput graph
const string Experiment_Result::write_throughput_column_name = "Average write throughput (IOs/s)";
const string Experiment_Result::read_throughput_column_name  = "Average read throughput (IOs/s)";
const string Experiment_Result::datafile_postfix 			= ".csv";
const string Experiment_Result::stats_filename 				= "stats";
const string Experiment_Result::waittime_filename_prefix 	= "waittime-";
const string Experiment_Result::age_filename_prefix 			= "age-";
const string Experiment_Result::queue_filename_prefix 		= "queue-";
const string Experiment_Result::throughput_filename_prefix   = "throughput-";
const string Experiment_Result::latency_filename_prefix      = "latency-";
const double Experiment_Result::M 							= 1000000.0; // One million
const double Experiment_Result::K 							= 1000.0;    // One thousand

Experiment_Result::Experiment_Result(string experiment_name, string data_folder, string sub_folder_, string variable_parameter_name)
:	experiment_name(experiment_name),
 	variable_parameter_name(variable_parameter_name),
 	max_age(0),
 	max_age_freq(0),
 	experiment_started(false),
 	experiment_finished(false),
	sub_folder(sub_folder_)
{
	this->data_folder = data_folder + sub_folder;
	stats_file = NULL;
 	max_waittimes = vector<double>(6,0);
 	graph_filename_prefix = "";
	//boost::filesystem::path working_dir = boost::filesystem::current_path();
    //boost::filesystem::create_directories(boost::filesystem::path(data_folder));
    //boost::filesystem::current_path(boost::filesystem::path(data_folder));
}

Experiment_Result::~Experiment_Result() {
	// Don't stop in the middle of an experiment. You finish what you have begun!
	assert((!experiment_started && !experiment_finished) || (experiment_started && experiment_finished));
}

void Experiment_Result::start_experiment() {
	assert(!experiment_started && !experiment_finished);
	experiment_started = true;
	start_time = Experiment::wall_clock_time();
    printf("=== Starting experiment '%s' ===\n", experiment_name.c_str());
    printf("%s\n", data_folder.c_str());
	mkdir(data_folder.c_str(), 0755);
    chdir(data_folder.c_str());

	// Write header of stat csv file
    stats_file = new std::ofstream();
    stats_file->open((stats_filename + datafile_postfix).c_str());
    (*stats_file) << "\"" << variable_parameter_name << "\", " << StatisticsGatherer::get_global_instance()->totals_csv_header() << ", \"" << throughput_column_name << "\", \"" << write_throughput_column_name << "\", \"" << read_throughput_column_name << "\"" << "\n";
}

void Experiment_Result::collect_stats(string variable_parameter_value) {
	collect_stats(variable_parameter_value, StatisticsGatherer::get_global_instance());
}

void Experiment_Result::collect_stats(string variable_parameter_value, StatisticsGatherer* statistics_gatherer) {
	assert(experiment_started && !experiment_finished);

	chdir(data_folder.c_str());

	points.push_back(variable_parameter_value);

	// Compute throughput
	int total_read_IOs_issued  = statistics_gatherer->total_reads();
	int total_write_IOs_issued = statistics_gatherer->total_writes();
	long double read_throughput = (long double) (statistics_gatherer->get_reads_throughput()) ; // IOs/sec
	long double write_throughput = (long double) (statistics_gatherer->get_writes_throughput() ); // IOs/sec
	long double total_throughput = write_throughput + read_throughput;

	(*stats_file) << variable_parameter_value << ", " << statistics_gatherer->totals_csv_line() << ", " << total_throughput << ", " << write_throughput << ", " << read_throughput << "\n";

	stringstream hist_filename;
	stringstream age_filename;
	stringstream queue_filename;
	stringstream throughput_filename;
	stringstream latency_filename;

	hist_filename << waittime_filename_prefix << variable_parameter_value << datafile_postfix;
	age_filename << age_filename_prefix << variable_parameter_value << datafile_postfix;
	queue_filename << queue_filename_prefix << variable_parameter_value << datafile_postfix;
	throughput_filename << throughput_filename_prefix << variable_parameter_value << datafile_postfix;
	latency_filename << latency_filename_prefix << variable_parameter_value << datafile_postfix;

	std::ofstream hist_file;
	hist_file.open(hist_filename.str().c_str());
	hist_file << statistics_gatherer->wait_time_histogram_all_IOs_csv();
	hist_file.close();
	vp_max_waittimes[variable_parameter_value] = statistics_gatherer->max_waittimes();
	for (uint i = 0; i < vp_max_waittimes[variable_parameter_value].size(); i++) {
		max_waittimes[i] = max(max_waittimes[i], vp_max_waittimes[variable_parameter_value][i]);
	}

	std::ofstream age_file;
	age_file.open(age_filename.str().c_str());
	age_file << SsdStatisticsExtractor::get_instance()->age_histogram_csv();
	age_file.close();
	max_age = max(max_age, SsdStatisticsExtractor::get_instance()->max_age());
	max_age_freq = max(max_age_freq, SsdStatisticsExtractor::get_instance()->max_age_freq());

	std::ofstream queue_file;
	queue_file.open(queue_filename.str().c_str());
	queue_file << statistics_gatherer->queue_length_csv();
	queue_file.close();

	std::ofstream throughput_file;
	queue_file.open(throughput_filename.str().c_str());
	queue_file << statistics_gatherer->app_and_gc_throughput_csv();
	queue_file.close();

	std::ofstream latency_file;
	queue_file.open(latency_filename.str().c_str());
	queue_file << statistics_gatherer->latency_csv();
	queue_file.close();

	map<string, StatisticData>::const_iterator it = StatisticData::statistics.begin();
	while (it != StatisticData::statistics.end()) {
		std::ofstream stat_file;
		stringstream file_name;
		file_name << (*it).first << "-" << variable_parameter_value << datafile_postfix;
		stat_file.open(file_name.str().c_str());
		stat_file << StatisticData::to_csv((*it).first);
		stat_file.close();
		it++;
	}

	//vp_num_IOs[variable_parameter_value].push_back(total_write_IOs_issued);
	//vp_num_IOs[variable_parameter_value].push_back(total_read_IOs_issued);
	//vp_num_IOs[variable_parameter_value].push_back(total_write_IOs_issued + total_read_IOs_issued);
}

void Experiment_Result::end_experiment() {
	assert(experiment_started && !experiment_finished);
	experiment_finished = true;

	stats_file->close();

	end_time = Experiment::wall_clock_time();

	printf("=== Experiment '%s' completed in %s. ===\n", experiment_name.c_str(), Experiment::pretty_time(time_elapsed()).c_str());
	printf("\n");

	vector<string> original_column_names = StatisticsGatherer::get_global_instance()->totals_vector_header();
	column_names.push_back(variable_parameter_name);
	column_names.insert(column_names.end(), original_column_names.begin(), original_column_names.end());
	column_names.push_back(throughput_column_name);
	column_names.push_back(write_throughput_column_name);
	column_names.push_back(read_throughput_column_name);

    //boost::filesystem::current_path(boost::filesystem::path(working_dir));

	delete stats_file;
}
