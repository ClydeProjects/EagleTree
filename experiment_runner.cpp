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
//#include <boost/filesystem.hpp>

#define SIZE 2

using namespace ssd;

const string Experiment_Runner::markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

const bool Experiment_Runner::REMOVE_GLE_SCRIPTS_AGAIN = false;

const string Experiment_Runner::datafile_postfix 			= ".csv";
const string Experiment_Runner::stats_filename 				= "stats";
const string Experiment_Runner::waittime_filename_prefix 	= "waittime-";
const string Experiment_Runner::age_filename_prefix 		= "age-";
const string Experiment_Runner::queue_filename_prefix 		= "queue-";

const double Experiment_Runner::M = 1000000.0; // One million
const double Experiment_Runner::K = 1000.0;    // One thousand

double Experiment_Runner::calibration_precision      = 1.0; // microseconds
double Experiment_Runner::calibration_starting_point = 50.0; // microseconds

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
		if (minutes == 1) time_text << " minute, ";
		else time_text << " minutes, ";
	}

	time_text << seconds;
	if (seconds == 1) time_text << " second ";
	else time_text << " seconds ";

	time_text << "[" << time << "s]";

	return time_text.str();
}

double Experiment_Runner::calibrate_IO_submission_rate(int highest_lba, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate)) {
	double max_rate = calibration_starting_point;
	double min_rate = 0;
	double current_rate;
	bool success;
	printf("Calibrating...\n");

	// finding an upper bound
	do {
		printf("Finding upper bound. Current is:  %f.\n", max_rate);
		success = true;
		vector<Thread*> threads = experiment(highest_lba, max_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		try        { os->run(); }
		catch(...) { success = false; max_rate *= 2; }
		delete os;
	} while (!success);

	do {
		current_rate = min_rate + ((max_rate - min_rate) / 2); // Pick a rate just between min and max
		printf("Optimal submission rate in range %f - %f. Trying %f.\n", min_rate, max_rate, current_rate);
		success = true;
		{
			vector<Thread*> threads = experiment(highest_lba, current_rate);
			OperatingSystem* os = new OperatingSystem(threads);
			try        { os->run(); }
			catch(...) { success = false; }
			delete os;
		}
		if      ( success) max_rate = current_rate;
		else if (!success) min_rate = current_rate;
	} while ((max_rate - min_rate) > calibration_precision);

	return max_rate;
}

Exp Experiment_Runner::overprovisioning_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int space_min, int space_max, int space_inc, string data_folder, string name) {
	uint max_age = 0;
	stringstream graph_name;

    vector<string> histogram_commands;

    mkdir(data_folder.c_str(), 0755);
    chdir(data_folder.c_str());

    //boost::filesystem::path working_dir = boost::filesystem::current_path();
    //boost::filesystem::create_directories(boost::filesystem::path(data_folder));
    //boost::filesystem::current_path(boost::filesystem::path(data_folder));

    string throughput_column_name = "Max sustainable throughput (IOs/s)";
	string measurement_name       = "Used space (%)";
    std::ofstream csv_file;
    csv_file.open(stats_filename.c_str());
    csv_file << "\"" << measurement_name << "\", " << StatisticsGatherer::get_instance()->totals_csv_header() << ", \"" << throughput_column_name <<"\"" << "\n";

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE;

    double start_time = wall_clock_time();

	for (int used_space = space_min; used_space <= space_max; used_space += space_inc) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("Experiment with max %d pct used space: Writing to no LBA higher than %d (out of %d total available)\n", used_space, highest_lba, num_pages);
		printf("----------------------------------------------------------------------------------------------------------\n");

		// Calibrate IO submission rate
		double IO_submission_rate = calibrate_IO_submission_rate(highest_lba, experiment);
		printf("Using IO submission rate of %f microseconds per IO\n", IO_submission_rate);

		// Run experiment
		vector<Thread*> threads = experiment(highest_lba, IO_submission_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		os->run();

		// Compute throughput, save statistics in csv format
		int total_IOs_issued = StatisticsGatherer::get_instance()->total_reads() + StatisticsGatherer::get_instance()->total_writes();
		long double throughput = (double) total_IOs_issued / os->get_total_runtime() * 1000; // IOs/sec
		csv_file << used_space << ", " << StatisticsGatherer::get_instance()->totals_csv_line() << ", " << throughput << "\n";

		stringstream hist_filename;
		stringstream age_filename;
		stringstream queue_filename;

		hist_filename << waittime_filename_prefix << used_space << datafile_postfix;
		age_filename << age_filename_prefix << used_space << datafile_postfix;
		queue_filename << queue_filename_prefix << used_space << datafile_postfix;

		std::ofstream hist_file;
		hist_file.open(hist_filename.str().c_str());
		hist_file << StatisticsGatherer::get_instance()->wait_time_histogram_csv();
		hist_file.close();

		std::ofstream age_file;
		age_file.open(age_filename.str().c_str());
		age_file << StatisticsGatherer::get_instance()->age_histogram_csv();
		age_file.close();
		max_age = max(StatisticsGatherer::get_instance()->max_age(), max_age);

		std::ofstream queue_file;
		queue_file.open(queue_filename.str().c_str());
		queue_file << StatisticsGatherer::get_instance()->queue_length_csv();
		queue_file.close();

		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	csv_file.close();

	double end_time = wall_clock_time();
	double time_elapsed = end_time - start_time;
	printf("Experiment completed in %s.\n", pretty_time(time_elapsed).c_str());

	vector<string> original_column_names = StatisticsGatherer::get_instance()->totals_vector_header();
	vector<string> column_names;
	column_names.push_back(measurement_name);
	column_names.insert(column_names.end(), original_column_names.begin(), original_column_names.end());
	column_names.push_back(throughput_column_name);

    //boost::filesystem::current_path(boost::filesystem::path(working_dir));

	return Exp(name, data_folder, "Free space (%)", column_names, max_age);
}

// Plotting x number of experiments into one graph
void Experiment_Runner::graph(int sizeX, int sizeY, string title, string filename, int column, vector<Exp> experiments) {
	// Write tempoary file containing GLE script
    string scriptFilename = filename + "-temp.gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());

    gleScript <<
    "size " << sizeX << " " << sizeY << endl << // 12 8
    "set font texcmr" << endl <<
    "begin graph" << endl <<
    "   key pos tl offset -0.0 0 compact" << endl <<
    "   scale auto" << endl <<
    "   title  \"" << title << "\"" << endl <<
    "   xtitle \"" << experiments[0].x_axis << "\"" << endl <<
    "   ytitle \"" << experiments[0].column_names[column] << "\"" << endl <<
    "   yaxis min 0" << endl;

    for (uint i = 0; i < experiments.size(); i++) {
    	Exp e = experiments[i];
        gleScript <<
       	"   data   \"" << e.data_folder << stats_filename << "\"" << " d"<<i+1<<"=c1,c" << column+1 << endl <<
        "   d"<<i+1<<" line marker " << markers[i] << " key " << "\"" << e.name << "\"" << endl;
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

void Experiment_Runner::waittime_boxplot(int sizeX, int sizeY, string title, string filename, int mean_column, Exp experiment) {
	// Write tempoary file containing GLE script
    string scriptFilename = filename + "-temp.gle"; // Name of tempoary script file
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
    "   xtitle \"" << experiment.x_axis << "\"" << endl <<
    "   ytitle \"Wait time (µs)\"" <<
	"   data   \"" << experiment.data_folder << stats_filename << "\"" << endl <<
    "	xaxis min dminx(d1)-2.5 max dmaxx(d1)+2.5 dticks 5" << endl << // nolast nofirst
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

void Experiment_Runner::waittime_histogram(int sizeX, int sizeY, string outputFile, Exp experiment, int points[]) {
	uint num_graphs = sizeof(points)/sizeof(int);

	vector<string> commands;
	for (uint i = 0; i < num_graphs; i++) {
		stringstream command;
		command << "hist 0 " << i << " \"" << waittime_filename_prefix << i << datafile_postfix << "\" \"Wait time histogram (" << experiment.x_axis << " = " << points[i] << ")\" \"log min 1\" \"Event wait time (µs)\" -1";
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}

void Experiment_Runner::age_histogram(int sizeX, int sizeY, string outputFile, Exp experiment, int points[]) {
	uint num_graphs = sizeof(points)/sizeof(int);

	vector<string> commands;

	stringstream age_max;
	age_max << "age_max = " << experiment.max_age << endl;
	commands.push_back(age_max.str());

	for (uint i = 0; i < num_graphs; i++) {
		stringstream command;
		command << "hist 0 " << i << " \"" << age_filename_prefix << i << datafile_postfix << "\" \"Block age histogram (" << experiment.x_axis << " = " << points[i] << ")\" \"on\" \"Block age\" age_max";
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}

void Experiment_Runner::queue_length_history(int sizeX, int sizeY, string outputFile, Exp experiment, int points[]) {
	uint num_graphs = sizeof(points)/sizeof(int);

	vector<string> commands;
	for (uint i = 0; i < num_graphs; i++) {
		stringstream command;
		command << "plot 0 " << i << " \"" << queue_filename_prefix << i << datafile_postfix << "\" \"Queue length history (" << experiment.x_axis << " = " << points[i] << ")\" \"on\" \"Timeline (µs progressed)\" \"Items in event queue\"";
		commands.push_back(command.str());
	}

	multigraph(sizeX, sizeY, outputFile, commands);
}
//endl <<
/*
	stringstream waittime_commands;
	waittime_commands << "hist 0 " << histogram_commands.size() << " \"" << hist_filename.str() << "\" \"Wait time histogram (" << used_space << "% used space)\" \"log min 1\" \"Event wait time (µs)\" -1";
	histogram_commands.push_back(waittime_commands.str());

	stringstream age_command;
	age_command << "hist 0 " << histogram_commands.size() << " \"" << age_filename.str() << "\" \"Block age histogram (" << used_space << "% used space)\" \"on\" \"Block age\" age_max";
	histogram_commands.push_back(age_command.str());

	stringstream queue_command;
	histogram_commands.push_back(queue_command.str());
*/


// Draw multiple smaller graphs in one image
void Experiment_Runner::multigraph(int sizeX, int sizeY, string outputFile, vector<string> commands) {
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
	"size std_sx+pad std_sy+(std_sy*hist_graphs/2)+pad" << endl <<
	"set font texcmr" << endl <<
	endl <<
	"sub hist xp yp data$ title$ yaxis$ xaxistitle$ xmax" << endl <<
	"   amove xp*(std_sx/2)+pad yp*(std_sy/2)+pad" << endl <<
	"   begin graph" << endl <<
	"      fullsize" << endl <<
	"      size 16-pad 5-pad" << endl <<
	"      key off" << endl <<
	"      data  data$" << endl <<
	"      title title$" << endl <<
	"      yaxis \\expr{yaxis$}" << endl <<
	"!      xaxis dticks 1" << endl <<
	"      if xmax<>-1 then" << endl <<
	"         xaxis max xmax" << endl <<
	"      end if" << endl <<
	"      xtitle xaxistitle$" << endl <<
	"      ytitle \"Frequency\"" << endl <<
	"      d1 line steps" << endl <<
	"   end graph" << endl <<
	"end sub" << endl <<
	endl <<
	"sub plot xp yp data$ title$ yaxis$ xaxistitle$ yaxistitle$" << endl <<
	"   amove xp*(std_sx/2)+pad yp*(std_sy/2)+pad" << endl <<
	"   begin graph" << endl <<
	"      fullsize" << endl <<
	"      size 16-pad 5-pad" << endl <<
	"      key off" << endl <<
	"      data  data$" << endl <<
	"      title title$" << endl <<
	"      yaxis \\expr{yaxis$}" << endl <<
	"      xtitle xaxistitle$" << endl <<
	"      ytitle yaxistitle$" << endl <<
	"      d1 line" << endl <<
	"   end graph" << endl <<
	"end sub" << endl;

    for (uint i = 0; i < commands.size(); i++) {
    	gleScript << commands[i] << endl;
    }

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

    if (REMOVE_GLE_SCRIPTS_AGAIN) remove(scriptFilename.c_str()); // Delete tempoary script file again
}
