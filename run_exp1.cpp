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

#define SIZE 2

using namespace ssd;

void DrawGraph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command) {
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

void DrawGraphWithHistograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands) {
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
	"size std_sx+pad std_sy+(std_sy*hist_graphs/2)+pad" << endl <<
	"set font texcmr" << endl <<
	endl <<
	"sub hist xp yp data$ title$ yaxis$" << endl <<
	"   amove xp*(std_sx/2)+pad yp*(std_sy/2)+pad" << endl <<
	"   begin graph" << endl <<
	"      fullsize" << endl <<
	"      size 16-pad 5-pad" << endl <<
	"      data  data$" << endl <<
	"      title title$" << endl <<
	"      yaxis \\expr{yaxis$}" << endl <<
	"      d1 line steps" << endl <<
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
	/*
    "hist 0 5 "hist-80.csv" "Wait time histogram" "log min 1"
	"hist 0 4 "age-80.csv" "Age histogram" "on"
	"hist 0 3 "hist-80.csv" "Wait time histogram" "log min 1"
	"hist 0 2 "age-80.csv" "Age histogram" "on"
	"hist 0 1 "hist-80.csv" "Wait time histogram" "log min 1"
	"hist 0 0 "age-80.csv" "Age histogram" "on"
	 */

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

//    remove(scriptFilename.c_str()); // Delete tempoary script file again
}
void overprovisioning_experiment() {
	string markers[] = {"circle", "square", "triangle", "diamond", "cross", "plus", "star", "star2", "star3", "star4", "flower"};

	// Experiment parameters ---------------------
	const int graph_data_types[]	= {9,10};
	int histogram_points[]			= {40,60,90};
    int space_min 					= 50;
	int space_max 					= 70;
	int space_inc 					= 5;
	const int num_random_IOs 	= 10000;
	// -------------------------------------------
    vector<string> histogram_commands;

    PRINT_LEVEL = 2;

	string measurement_name = "Used space (%)";
	string csv_filename = "overprovisioning_experiment.csv";
    std::ofstream csv_file;
    csv_file.open(csv_filename.c_str());
    csv_file << "\"" << measurement_name << "\", " << StatisticsGatherer::get_instance()->totals_csv_header() << ", \"Throughput (IOs/µs)\"" << "\n";

    int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	double time_breaks, total_runtime, average_time_per_write;
	int total_writes_issued;
	for (int used_space = space_min; used_space <= space_max; used_space += space_inc) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("Experiment with max %d pct used space: Writing to no LBA higher than %d (out of %d total available)\n", used_space, highest_lba, num_pages);
		printf("----------------------------------------------------------------------------------------------------------\n");

		time_breaks = 10.0;

		{
			Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba-1, 1, WRITE, time_breaks, 0);
			t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_random_IOs, 1, WRITE, time_breaks, 0));
			t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_random_IOs, 2, READ, time_breaks, 0));

			vector<Thread*> threads;
			threads.push_back(t1);

			OperatingSystem* os = new OperatingSystem(threads);
			os->run();

			total_runtime          = os->get_total_runtime();
			delete os;
		}

		total_writes_issued       = highest_lba + num_random_IOs;
		average_time_per_write = total_runtime / (double) total_writes_issued;
		time_breaks = average_time_per_write;
		printf("Total writes: %d   Total runtime: %f   Avg.time/write: %f\n", total_writes_issued, total_runtime, average_time_per_write);


		{
			Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba-1, 1, WRITE, time_breaks, 0);
			t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_random_IOs, 1, WRITE, time_breaks, 0));
			t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_random_IOs, 2, READ, time_breaks, 0));

			vector<Thread*> threads;
			threads.push_back(t1);

			OperatingSystem* os = new OperatingSystem(threads);
			os->run();

			double throughput = (double) total_writes_issued / total_runtime;
			time_breaks = average_time_per_write;

			csv_file << used_space << ", " << StatisticsGatherer::get_instance()->totals_csv_line() << ", " << throughput << "\n";

			if (count (histogram_points, histogram_points+sizeof(histogram_points)/sizeof(int), used_space) == 1) {
				stringstream hist_filename;
				stringstream age_filename;
				hist_filename << "hist-" << used_space << ".csv";
				age_filename << "age-" << used_space << ".csv";
				std::ofstream hist_file;
				hist_file.open(hist_filename.str().c_str());
				hist_file << StatisticsGatherer::get_instance()->wait_time_histogram_csv();
				hist_file.close();

				std::ofstream age_file;
				age_file.open(age_filename.str().c_str());
				age_file << StatisticsGatherer::get_instance()->age_histogram_csv();
				age_file.close();

			    stringstream hist_command;
			    hist_command << "hist 0 " << histogram_commands.size() << " \"" << hist_filename.str() << "\" \"Wait time histogram (" << used_space << "% used space)\" \"log min 1\"";
			    histogram_commands.push_back(hist_command.str());

			    stringstream age_command;
			    age_command << "hist 0 " << histogram_commands.size() << " \"" << age_filename.str() << "\" \"Block age histogram (" << used_space << "% used space)\" \"on\"";
			    histogram_commands.push_back(age_command.str());

			}

			StateVisualiser::print_page_status();
			StateVisualiser::print_block_ages();

			delete os;
		}

	}

	stringstream command;
	for (uint i = 0; i < sizeof(graph_data_types)/sizeof(int); i++) {
		command << "   " << "d" << graph_data_types[i] << " line marker " << markers[i] << "\n";
    }

	stringstream graph_name;
    graph_name << "Overprovisioning experiment (" << num_random_IOs << " random writes)";// + " << num_random_IOs << " random reads)";
	csv_file.close();
	DrawGraphWithHistograms(
    	16, 10, "overprovisioning_experiment", csv_filename, graph_name.str(),
    	measurement_name, "units", "",
    	command.str(),
    	histogram_commands
    );

}

int main()
{
	load_config();
	print_config(NULL);
	printf("\n");

	overprovisioning_experiment();

	return 0;
}
