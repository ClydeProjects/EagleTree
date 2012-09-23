/* Copyright 2009, 2010 Brendan Tauras */

/* run_test2.cpp is part of FlashSim. */

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

/* Basic test driver
 * Brendan Tauras 2010-08-03
 *
 * driver to create and run a very basic test of writes then reads */
#define BOOST_FILESYSTEM_VERSION 3
#include "ssd.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>  /* defines FILENAME_MAX */

//#include <boost/filesystem.hpp>

#define SIZE 2

using namespace ssd;

const string Experiment_Runner::markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

const bool Experiment_Runner::REMOVE_GLE_SCRIPTS_AGAIN = false;

const double Experiment_Runner::M = 1000000.0; // One million
const double Experiment_Runner::K = 1000.0;    // One thousand

double Experiment_Runner::calibration_precision      = 1.0; // microseconds
double Experiment_Runner::calibration_starting_point = 15.00; // microseconds

const string ExperimentResult::datafile_postfix 			= ".csv";
const string ExperimentResult::stats_filename 				= "stats";
const string ExperimentResult::waittime_filename_prefix 	= "waittime-";
const string ExperimentResult::age_filename_prefix 			= "age-";
const string ExperimentResult::queue_filename_prefix 		= "queue-";
const string ExperimentResult::throughput_filename_prefix   = "throughput-";
const double ExperimentResult::M 							= 1000000.0; // One million
const double ExperimentResult::K 							= 1000.0;    // One thousand

ExperimentResult::ExperimentResult(string experiment_name, string data_folder_, string variable_parameter_name, string throughput_column_name)
:	experiment_name(experiment_name),
 	variable_parameter_name(variable_parameter_name),
 	max_age(0),
 	max_age_freq(0),
 	throughput_column_name(throughput_column_name),
 	experiment_started(false),
 	experiment_finished(false)
{
	working_dir = Experiment_Runner::get_working_dir();
	data_folder = working_dir + "/" + data_folder_;
 	stats_file = NULL;

	//boost::filesystem::path working_dir = boost::filesystem::current_path();
    //boost::filesystem::create_directories(boost::filesystem::path(data_folder));
    //boost::filesystem::current_path(boost::filesystem::path(data_folder));
}

ExperimentResult::~ExperimentResult() {
	// Don't stop in the middle of an experiment. You finish what you have begun!
	assert((!experiment_started && !experiment_finished) || (experiment_started && experiment_finished));
}

void ExperimentResult::start_experiment() {
	assert(!experiment_started && !experiment_finished);
	experiment_started = true;
	start_time = Experiment_Runner::wall_clock_time();
    printf("=== Starting experiment '%s' ===\n", experiment_name.c_str());

	mkdir(data_folder.c_str(), 0755);
    chdir(data_folder.c_str());

	// Write header of stat csv file
    stats_file = new std::ofstream();
    stats_file->open((stats_filename + datafile_postfix).c_str());
    (*stats_file) << "\"" << variable_parameter_name << "\", " << StatisticsGatherer::get_instance()->totals_csv_header() << ", \"" << throughput_column_name << "\"" << "\n";
}

void ExperimentResult::collect_stats(uint variable_parameter_value, long double throughput) {
	assert(experiment_started && !experiment_finished);

	(*stats_file) << variable_parameter_value << ", " << StatisticsGatherer::get_instance()->totals_csv_line() << ", " << throughput << "\n";

	stringstream hist_filename;
	stringstream age_filename;
	stringstream queue_filename;
	stringstream throughput_filename;

	hist_filename << waittime_filename_prefix << variable_parameter_value << datafile_postfix;
	age_filename << age_filename_prefix << variable_parameter_value << datafile_postfix;
	queue_filename << queue_filename_prefix << variable_parameter_value << datafile_postfix;
	throughput_filename << throughput_filename_prefix << variable_parameter_value << datafile_postfix;

	std::ofstream hist_file;
	hist_file.open(hist_filename.str().c_str());
	hist_file << StatisticsGatherer::get_instance()->wait_time_histogram_csv();
	hist_file.close();

	std::ofstream age_file;
	age_file.open(age_filename.str().c_str());
	age_file << StatisticsGatherer::get_instance()->age_histogram_csv();
	age_file.close();
	max_age = max(StatisticsGatherer::get_instance()->max_age(), max_age);
	max_age_freq = max(StatisticsGatherer::get_instance()->max_age_freq(), max_age_freq);

	std::ofstream queue_file;
	queue_file.open(queue_filename.str().c_str());
	queue_file << StatisticsGatherer::get_instance()->queue_length_csv();
	queue_file.close();

	std::ofstream throughput_file;
	queue_file.open(throughput_filename.str().c_str());
	queue_file << StatisticsGatherer::get_instance()->app_and_gc_throughput_csv();
	queue_file.close();

}

void ExperimentResult::end_experiment() {
	assert(experiment_started && !experiment_finished);
	experiment_finished = true;

	stats_file->close();

	end_time = Experiment_Runner::wall_clock_time();

	printf("=== Experiment '%s' completed in %s. ===\n", experiment_name.c_str(), Experiment_Runner::pretty_time(time_elapsed()).c_str());
	printf("\n");

	vector<string> original_column_names = StatisticsGatherer::get_instance()->totals_vector_header();
	column_names.push_back(variable_parameter_name);
	column_names.insert(column_names.end(), original_column_names.begin(), original_column_names.end());
	column_names.push_back(throughput_column_name);

    //boost::filesystem::current_path(boost::filesystem::path(working_dir));

	chdir(working_dir.c_str());

	delete stats_file;
}


double Experiment_Runner::CPU_time_user() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    struct timeval time = ru.ru_utime;

    // Calculate time in seconds
    double result = time.tv_sec + (time.tv_usec / M);
    return result;
}

double Experiment_Runner::CPU_time_system() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    struct timeval time = ru.ru_stime;

    // Calculate time in seconds
    double result = time.tv_sec + (time.tv_usec / M);
    return result;
}

double Experiment_Runner::wall_clock_time() {
    struct timeval time;
    gettimeofday(&time, NULL);

    // Calculate time in seconds
    double result = time.tv_sec + time.tv_usec / M;
    return result;
}

string Experiment_Runner::pretty_time(double time) {
	stringstream time_text;
	uint hours = (uint) floor(time / 3600.0);
	uint minutes = (uint) floor(fmod(time, 3600.0)/60.0);
	double seconds = fmod(time, 60.0);
	if (hours > 0) {
		time_text << hours;
		if (hours == 1) time_text << " hour, ";
		else time_text << " hours, ";
	}

	if (minutes > 0 || hours > 0) {
		time_text << minutes;
		if (minutes == 1) time_text << " minute and ";
		else time_text << " minutes and ";
	}

	time_text << seconds;
	if (seconds == 1) time_text << " second ";
	else time_text << " seconds ";

	time_text << "[" << time << "s]";

	return time_text.str();
}

double Experiment_Runner::measure_throughput(int highest_lba, double IO_submission_rate, int IO_limit, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate)) {
	vector<Thread*> threads = experiment(highest_lba, IO_submission_rate);
	OperatingSystem* os = new OperatingSystem(threads);
	os->set_num_writes_to_stop_after(IO_limit);
	os->run();
	int total_IOs_issued = StatisticsGatherer::get_instance()->total_reads() + StatisticsGatherer::get_instance()->total_writes();
	return (double) total_IOs_issued / os->get_total_runtime();
}

double Experiment_Runner::calibrate_IO_submission_rate_queue_based(int highest_lba, int IO_limit, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate)) {
	double max_rate = calibration_starting_point;
	double min_rate = 0;
	double current_rate;
	bool success;
	printf("Calibrating...\n");

	// Finding an upper bound
	do {
		printf("Finding upper bound. Current is:  %f.\n", max_rate);
		success = true;
		vector<Thread*> threads = experiment(highest_lba, max_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		try        { os->run(); }
		catch(...) { success = false; min_rate = max_rate; max_rate *= 2; }
		delete os;
	} while (!success);

	while ((max_rate - min_rate) > calibration_precision)
	{
		current_rate = min_rate + ((max_rate - min_rate) / 2); // Pick a rate just between min and max
		printf("Optimal submission rate in range %f - %f. Trying %f.\n", min_rate, max_rate, current_rate);
		success = true;
		{
			vector<Thread*> threads = experiment(highest_lba, current_rate);
			OperatingSystem* os = new OperatingSystem(threads);
			os->set_num_writes_to_stop_after(IO_limit);
			try        { os->run(); }
			catch(...) { success = false; }
			delete os;
		}
		if      ( success) max_rate = current_rate;
		else if (!success) min_rate = current_rate;
	}

	return max_rate;
}

ExperimentResult Experiment_Runner::overprovisioning_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int space_min, int space_max, int space_inc, string data_folder, string name, int IO_limit) {
    ExperimentResult experiment_result(name, data_folder, "Used space (%)", "Max sustainable throughput (IOs/s)");
    experiment_result.start_experiment();

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
    for (int used_space = space_min; used_space <= space_max; used_space += space_inc) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("Experiment with max %d pct used space: Writing to no LBA higher than %d (out of %d total available)\n", used_space, highest_lba, num_pages);
		printf("----------------------------------------------------------------------------------------------------------\n");

		// Calibrate IO submission rate
		double IO_submission_rate = 10; //calibrate_IO_submission_rate_queue_based(highest_lba, IO_limit, experiment);
		printf("Using IO submission rate of %f microseconds per IO\n", IO_submission_rate);

		// Run experiment
		vector<Thread*> threads = experiment(highest_lba, IO_submission_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		os->run();

		// Compute throughput
		int total_IOs_issued = StatisticsGatherer::get_instance()->total_reads() + StatisticsGatherer::get_instance()->total_writes();
		long double throughput = (double) total_IOs_issued / os->get_total_runtime() * 1000; // IOs/sec

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(used_space, throughput);

		// Print shit
		StatisticsGatherer::get_instance()->print();
		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	experiment_result.end_experiment();
	return experiment_result;
}

ExperimentResult Experiment_Runner::copyback_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int used_space, int max_copybacks, string data_folder, string name, int IO_limit) {
    ExperimentResult experiment_result(name, data_folder, "CopyBacks allowed before ECC check", "Max sustainable throughput (IOs/s)");
    experiment_result.start_experiment();

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;
    for (int copybacks_allowed = 0; copybacks_allowed <= max_copybacks; copybacks_allowed += 1) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("---------------------------------------\n");
		printf("Experiment with %d copybacks allowed.\n", copybacks_allowed);
		printf("---------------------------------------\n");

		MAX_REPEATED_COPY_BACKS_ALLOWED = copybacks_allowed;

		// Calibrate IO submission rate
		double IO_submission_rate = calibrate_IO_submission_rate_queue_based(highest_lba, IO_limit, experiment);
		printf("Using IO submission rate of %f microseconds per IO\n", IO_submission_rate);

		// Run experiment
		vector<Thread*> threads = experiment(highest_lba, IO_submission_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		os->set_num_writes_to_stop_after(IO_limit);
		os->run();

		// Compute throughput
		int total_IOs_issued = StatisticsGatherer::get_instance()->total_reads() + StatisticsGatherer::get_instance()->total_writes();
		long double throughput = (double) total_IOs_issued / os->get_total_runtime() * 1000; // IOs/sec

		// Collect statistics from this experiment iteration (save in csv files)
		experiment_result.collect_stats(copybacks_allowed, throughput);

		// Print shit
		StatisticsGatherer::get_instance()->print();
		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	experiment_result.end_experiment();
	return experiment_result;
}

// Plotting x number of experiments into one graph
void Experiment_Runner::graph(int sizeX, int sizeY, string title, string filename, int column, vector<ExperimentResult> experiments) {
	// Write tempoary file containing GLE script
    string scriptFilename = filename + ".gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());

    gleScript <<
    "size " << sizeX << " " << sizeY << endl << // 12 8
    "set font texcmr" << endl <<
    "begin graph" << endl <<
    "   key pos bl offset -0.0 0 compact" << endl <<
    "   scale auto" << endl <<
    "   title  \"" << title << "\"" << endl <<
    "   xtitle \"" << experiments[0].variable_parameter_name << "\"" << endl <<
    "   ytitle \"" << experiments[0].column_names[column] << "\"" << endl <<
    "   yaxis min 0" << endl;

    for (uint i = 0; i < experiments.size(); i++) {
    	ExperimentResult e = experiments[i];
        gleScript <<
       	"   data   \"" << e.data_folder << ExperimentResult::stats_filename << ExperimentResult::datafile_postfix << "\"" << " d"<<i+1<<"=c1,c" << column+1 << endl <<
        "   d"<<i+1<<" line marker " << markers[i] << " key " << "\"" << e.experiment_name << "\"" << endl;
    }

    gleScript <<
    "end graph" << endl;
    gleScript.close();

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + filename + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

    if (REMOVE_GLE_SCRIPTS_AGAIN) remove(scriptFilename.c_str()); // Delete tempoary script file again
}

void Experiment_Runner::waittime_boxplot(int sizeX, int sizeY, string title, string filename, int mean_column, ExperimentResult experiment) {
	chdir(experiment.data_folder.c_str());

	// Write tempoary file containing GLE script
    string scriptFilename = filename + ".gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());

    gleScript <<
    "size " << sizeX << " " << sizeY << endl << // 12 8
    "include \"graphutil.gle\"" << endl <<
    "set font texcmr" << endl <<
    "begin graph" << endl <<
    "   key pos tl offset -0.0 0 compact" << endl <<
    "   scale auto" << endl <<
    "   title  \"" << title << "\"" << endl <<
    "   xtitle \"" << experiment.variable_parameter_name << "\"" << endl <<
    "   ytitle \"Wait time (µs)\"" << endl <<
	"   data \"" << experiment.data_folder << ExperimentResult::stats_filename << ExperimentResult::datafile_postfix << "\"" << endl <<
    "   xaxis min dminx(d1)-2.5 max dmaxx(d1)+2.5 dticks 5" << endl << // nolast nofirst
    "   dticks off" << endl <<
    "   yaxis min 0 max dmaxy(d" << mean_column+5 << ")*1.05" << endl << // mean_column+5 = max column
    "   draw boxplot bwidth 0.4 ds0 " << mean_column << endl;

    gleScript <<
    "end graph" << endl;
    gleScript.close();

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + filename + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

    if (REMOVE_GLE_SCRIPTS_AGAIN) remove(scriptFilename.c_str()); // Delete tempoary script file again
}

void Experiment_Runner::draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command) {
    // Write tempoary file containing GLE script
    string scriptFilename = outputFile + ".gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());
    gleScript <<
    "size " << sizeX << " " << sizeY << endl << // 12 8
    "set font texcmr" << endl <<
    "begin graph" << endl <<
    "   " << "key pos tl offset -0.0 0 compact" << endl <<
    "   scale auto" << endl <<
//    "   nobox" << endl <<
    "   title  \"" << title << "\"" << endl <<
    "   xtitle \"" << xAxisTitle << "\"" << endl <<
    "   ytitle \"" << yAxisTitle << "\"" << endl <<
//    "   xticks off" << endl <<
    "   " << xAxisConf << endl <<
    "   yaxis min 0" << endl <<
    "   data   \"" << dataFilename << "\"" << endl <<
    command << endl <<
    "end graph" << endl;
    gleScript.close();

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

    if (REMOVE_GLE_SCRIPTS_AGAIN) remove(scriptFilename.c_str()); // Delete tempoary script file again
}

void Experiment_Runner::waittime_histogram(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points) {
	vector<string> commands;
	for (uint i = 0; i < points.size(); i++) {
		stringstream command;
		command << "hist 0 " << i << " \"" << ExperimentResult::waittime_filename_prefix << points[i] << ExperimentResult::datafile_postfix << "\" \"Wait time histogram (" << experiment.variable_parameter_name << " = " << points[i] << ")\" \"log min 1\" \"Event wait time (µs)\" -1 " << StatisticsGatherer::get_instance()->get_wait_time_histogram_bin_size();
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}

void Experiment_Runner::age_histogram(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points) {
	vector<string> settings;
	stringstream age_max;
	age_max << "age_max = " << experiment.max_age;
	settings.push_back(age_max.str());

	vector<string> commands;
	for (uint i = 0; i < points.size(); i++) {
		stringstream command;
		command << "hist 0 " << i << " \"" << ExperimentResult::age_filename_prefix << points[i] << ExperimentResult::datafile_postfix << "\" \"Block age histogram (" << experiment.variable_parameter_name << " = " << points[i] << ")\" \"on min 0 max " << experiment.max_age_freq << "\" \"Block age\" age_max " << StatisticsGatherer::get_instance()->get_age_histogram_bin_size();
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands, settings);
}

void Experiment_Runner::queue_length_history(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points) {
	vector<string> commands;
	for (uint i = 0; i < points.size(); i++) {
		stringstream command;
		command << "plot 0 " << i << " \"" << ExperimentResult::queue_filename_prefix << points[i] << ExperimentResult::datafile_postfix << "\" \"Queue length history (" << experiment.variable_parameter_name << " = " << points[i] << ")\" \"on\" \"Timeline (µs progressed)\" \"Items in event queue\"";
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}

void Experiment_Runner::throughput_history(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points) {
	vector<string> commands;
	for (uint i = 0; i < points.size(); i++) {
		stringstream command;
		command << "plot 0 " << i << " \"" << ExperimentResult::throughput_filename_prefix << points[i] << ExperimentResult::datafile_postfix << "\" \"Throughput history (" << experiment.variable_parameter_name << " = " << points[i] << ")\" \"on\" \"Timeline (µs progressed)\" \"Throughput (IOs/s)\" " << 2;
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}

// Draw multiple smaller graphs in one image
void Experiment_Runner::multigraph(int sizeX, int sizeY, string outputFile, vector<string> commands, vector<string> settings) {
	// Write tempoary file containing GLE script
    string scriptFilename = outputFile + ".gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());

	gleScript <<
	"std_sx = " << sizeX << endl <<
	"std_sy = " << sizeY << endl <<
	endl <<
	"hist_graphs = " << commands.size() << endl <<
	endl <<
	"pad = 2" << endl <<
	endl <<
	"size std_sx+pad std_sy*hist_graphs+pad" << endl <<
	"set font texcmr" << endl <<
	endl <<
	"sub hist xp yp data$ title$ yaxis$ xaxistitle$ xmax binsize" << endl <<
	"   amove xp*(std_sx/2)+pad yp*std_sy+pad" << endl <<
	"   begin graph" << endl <<
	"      fullsize" << endl <<
	"      size std_sx-pad std_sy-pad" << endl <<
	"      key off" << endl <<
	"      data  data$" << endl <<
	"      title title$" << endl <<
	"      yaxis \\expr{yaxis$}" << endl <<
	"      xaxis dticks int(xmax/25+1)" << endl <<
	"      xaxis min -binsize/2" << endl <<
	"      xsubticks off" << endl <<
	"      x2ticks off" << endl <<
	"      x2subticks off" << endl <<
	"      xticks length -.1" << endl <<
	"      if xmax>0 then" << endl <<
	"         xaxis max xmax+binsize/2" << endl <<
	"      end if" << endl <<
	"      xtitle xaxistitle$" << endl <<
	"      ytitle \"Frequency\"" << endl <<
	"      bar d1 width binsize dist binsize fill gray" << endl <<
	"   end graph" << endl <<
	"end sub" << endl <<
	endl <<
	"sub plot xp yp data$ title$ yaxis$ xaxistitle$ yaxistitle$ num_plots" << endl <<
	"   default num_plots 1" << endl <<
	"   amove xp*(std_sx/2)+pad yp*std_sy+pad" << endl <<
	"   begin graph" << endl <<
	"      fullsize" << endl <<
	"      size std_sx-pad std_sy-pad" << endl <<
	"      if num_plots <= 1 then" << endl <<
	"         key off" << endl <<
	"      end if" << endl <<
	"      data  data$" << endl <<
	"      title title$" << endl <<
	"      yaxis \\expr{yaxis$}" << endl <<
	"      xtitle xaxistitle$" << endl <<
	"      ytitle yaxistitle$" << endl <<
	"      d1 line" << endl <<
	"      if num_plots >= 2 then" << endl <<
	"         d2 line color red" << endl <<
	"      end if" << endl <<
	"   end graph" << endl <<
	"end sub" << endl;

    for (uint i = 0; i < settings.size(); i++) {
    	gleScript << settings[i] << endl;
    }
    for (uint i = 0; i < commands.size(); i++) {
    	gleScript << commands[i] << endl;
    }

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

    if (REMOVE_GLE_SCRIPTS_AGAIN) remove(scriptFilename.c_str()); // Delete tempoary script file again
}

string Experiment_Runner::get_working_dir() {
	char cCurrentPath[FILENAME_MAX];
	if (!getcwd(cCurrentPath, sizeof(cCurrentPath))) { return "<?>"; }
	cCurrentPath[sizeof(cCurrentPath) - 1] = '\0'; /* not really required */
	string currentPath(cCurrentPath);
	return currentPath;
}
