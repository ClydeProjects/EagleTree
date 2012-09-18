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

#include "ssd.h"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#define SIZE 2

using namespace ssd;

static uint max_age = 0;

static const double M = 1000000.0; // One million
static const double K = 1000.0;    // One thousand

static double calibration_precision = 0.1;        // microseconds
static double calibration_starting_point = 100.0; // microseconds


double CPU_time_user() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    struct timeval time = ru.ru_utime;

    // Calculate time in seconds
    double result = time.tv_sec + (time.tv_usec / M);
    return result;
}

double CPU_time_system() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    struct timeval time = ru.ru_stime;

    // Calculate time in seconds
    double result = time.tv_sec + (time.tv_usec / M);
    return result;
}

double wall_clock_time() {
    struct timeval time;
    gettimeofday(&time, NULL);

    // Calculate time in seconds
    double result = time.tv_sec + time.tv_usec / M;
    return result;
}

string pretty_time(double time) {
	stringstream time_text;
	double t = time;
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

void draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command) {
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

//    remove(scriptFilename.c_str()); // Delete tempoary script file again
}

void draw_graph_with_histograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands) {
    // Write tempoary file containing GLE script
    string scriptFilename = outputFile + ".gle"; // Name of tempoary script file
    std::ofstream gleScript;
    gleScript.open(scriptFilename.c_str());
    gleScript <<
	"std_sx = " << sizeX << endl <<
	"std_sy = " << sizeY << endl <<
	endl <<
	"hist_graphs = " << histogram_commands.size() << endl <<
	endl <<
	"pad = 2" << endl <<
	endl <<
	"age_max = " << max_age << endl <<
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
	"end sub" << endl <<
	endl <<
	"amove 0+pad (std_sy*hist_graphs/2)+pad" << endl <<
	"begin graph" << endl <<
	"   fullsize" << endl <<
	"   key pos tr offset -0.0 0 compact" << endl <<
	"   size std_sx-pad std_sy-pad" << endl <<
    "   title  \"" << title << "\"" << endl <<
    "   xtitle \"" << xAxisTitle << "\"" << endl <<
    "   ytitle \"" << yAxisTitle << "\"" << endl <<
	endl <<
	"   yaxis min 0" << endl <<
    "   data   \"" << dataFilename << "\"" << endl <<
	"   d11 line marker circle" << endl <<
	"end graph" << endl <<
	endl;
    for (int i = 0; i < histogram_commands.size(); i++) {
    	gleScript << histogram_commands[i] << endl;
    }

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

//    remove(scriptFilename.c_str()); // Delete tempoary script file again
}

vector<Thread*> random_IO_experiment(int highest_lba, int num_IOs, double IO_submission_rate) {
	Thread* t1 = new Asynchronous_Sequential_Thread(0, 0/*highest_lba-1*/, 1, WRITE, IO_submission_rate, 1);
	t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_IOs, 1, WRITE, IO_submission_rate, 1));
	//t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_IOs, 2, READ, IO_submission_rate, 0));

	vector<Thread*> threads;
	threads.push_back(t1);
	return threads;
}

double calibrate_IO_submission_rate(int highest_lba, int num_IOs, vector<Thread*> (*experiment)(int highest_lba, int num_IOs, double IO_submission_rate)) {
	double max_rate = calibration_starting_point;
	double min_rate = 0;
	double rate = min_rate + (max_rate - min_rate) / 2;
	bool success;
	printf("Calibrating...\n");
	do {
		printf("Optimal submission rate in range %f - %f\n", min_rate, max_rate);
		success = true;
		{
			vector<Thread*> threads = experiment(highest_lba, num_IOs, rate);
			OperatingSystem* os = new OperatingSystem(threads);
			try { os->run(); }
			catch(...) { success = false; }
			delete os;
		}
		if (success) {
			max_rate = rate;
			rate -= ((max_rate - min_rate) / 2);
		} else if (!success) {
			min_rate = rate;
			rate += ((max_rate - min_rate) / 2);
		}
	} while ((max_rate - min_rate) > calibration_precision);

	return max_rate;
}

void overprovisioning_experiment(vector<Thread*> (*experiment)(int highest_lba, int num_IOs, double IO_submission_rate), string data_folder) {
	string markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

	// Experiment parameters ----------------------------------------------
	const int graph_data_types[]					= {11};     // Draw these values on main graph (numbers correspond to each type of StatisticsGatherer output)
	const int details_graphs_for_used_space[]		= {50,70,90}; // Draw age and wait time histograms plus queue length history for chosen used_space values
	const int space_min								= 90;
    const int space_max								= 90;
	const int space_inc								= 5;
	const int num_IOs								= 100000;
    PRINT_LEVEL										= 0;
	stringstream graph_name;
    graph_name << "Overprovisioning experiment (" << num_IOs << " random writes + " << num_IOs << " random reads)";
    // --------------------------------------------------------------------

	// -- unused for now, calibrating throughput instead --
	const long long cpu_instructions_per_sec		= 21000 * 1000000ll; // Benchmark by Mathias (3.5GHz i7 3970K)
	const long long cpu_instructions_used_per_io	= 4468; // Benchmark by Mathias
	// -------------------------------------------
    long double IOs_per_microsecond	= (long double) cpu_instructions_per_sec / 1000000ll / cpu_instructions_used_per_io ;
	double IO_submission_rate	= 12.0 / IOs_per_microsecond;
    vector<string> histogram_commands;

    chdir(data_folder.c_str());

	string measurement_name = "Used space (%)";
	string csv_filename = "overprovisioning_experiment.csv";
    std::ofstream csv_file;
    csv_file.open(csv_filename.c_str());
    csv_file << "\"" << measurement_name << "\", " << StatisticsGatherer::get_instance()->totals_csv_header() << ", \"Throughput (IOs/s)\"" << "\n";

    const int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

    double start_time = wall_clock_time();

	for (int used_space = space_min; used_space <= space_max; used_space += space_inc) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("Experiment with max %d pct used space: Writing to no LBA higher than %d (out of %d total available)\n", used_space, highest_lba, num_pages);
		printf("----------------------------------------------------------------------------------------------------------\n");

		IO_submission_rate = calibrate_IO_submission_rate(highest_lba, num_IOs, experiment);

		printf("Using IO submission rate of %f microseconds per IO\n", IO_submission_rate);
		vector<Thread*> threads = experiment(highest_lba, num_IOs, IO_submission_rate);
		OperatingSystem* os = new OperatingSystem(threads);
		os->run();

		int total_IOs_issued = StatisticsGatherer::get_instance()->total_reads() + StatisticsGatherer::get_instance()->total_writes();
		long double throughput = (double) total_IOs_issued / os->get_total_runtime() * 1000; // IOs/sec
		csv_file << used_space << ", " << StatisticsGatherer::get_instance()->totals_csv_line() << ", " << throughput << "\n";

		if (count (details_graphs_for_used_space, details_graphs_for_used_space+sizeof(details_graphs_for_used_space)/sizeof(int), used_space) >= 1) {
			stringstream hist_filename;
			stringstream age_filename;
			stringstream queue_filename;

			hist_filename << "hist-" << used_space << ".csv";
			age_filename << "age-" << used_space << ".csv";
			queue_filename << "queue-" << used_space << ".csv";

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

			stringstream hist_command;
			hist_command << "hist 0 " << histogram_commands.size() << " \"" << hist_filename.str() << "\" \"Wait time histogram (" << used_space << "% used space)\" \"log min 1\" \"Event wait time (µs)\" -1";
			histogram_commands.push_back(hist_command.str());

			stringstream age_command;
			age_command << "hist 0 " << histogram_commands.size() << " \"" << age_filename.str() << "\" \"Block age histogram (" << used_space << "% used space)\" \"on\" \"Block age\" age_max";
			histogram_commands.push_back(age_command.str());

			stringstream queue_command;
			queue_command << "plot 0 " << histogram_commands.size() << " \"" << queue_filename.str() << "\" \"Queue length history (" << used_space << "% used space)\" \"on\" \"Timeline (µs progressed)\" \"Items in event queue\"";
			histogram_commands.push_back(queue_command.str());
		}

		if (PRINT_LEVEL >= 1) {
			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();
		}

		delete os;
	}

	double end_time = wall_clock_time();

	double time_elapsed = end_time - start_time;

	printf("Experiment completed in %s.\n", pretty_time(time_elapsed).c_str());

	stringstream command;
	for (uint i = 0; i < sizeof(graph_data_types)/sizeof(int); i++) {
		command << "   " << "d" << graph_data_types[i] << " line marker " << markers[i] << "\n";
    }

	csv_file.close();
	draw_graph_with_histograms(
    	16, 10, "overprovisioning_experiment", csv_filename, graph_name.str(),
    	measurement_name, "", "",
    	command.str(),
    	histogram_commands
    );

}

int main()
{
	load_config();
	print_config(NULL);

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;

	overprovisioning_experiment(random_IO_experiment, "/home/mkja/git/EagleTree/Exp1/");

	PACKAGE_SIZE = 16;

	overprovisioning_experiment(random_IO_experiment, "/home/mkja/git/EagleTree/Exp2/");

	return 0;
}
