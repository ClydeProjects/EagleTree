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
    "   " << command << endl <<
    "end graph" << endl;
    gleScript.close();

    // Run gle to draw graph
    string gleCommand = "gle \"" + scriptFilename + "\" \"" + outputFile + "\"";
    cout << gleCommand << "\n";
    system(gleCommand.c_str());

//    remove(scriptFilename.c_str()); // Delete tempoary script file again
}

void overprovisioning_experiment() {
    const int num_random_writes = 100000;

    string measurement_name = "Used space (%)";
	string csv_filename = "overprovisioning_experiment.csv";
    std::ofstream csv_file;
    csv_file.open(csv_filename.c_str());
    csv_file << "\"" << measurement_name << "\"," << StatisticsGatherer::get_instance()->totals_csv_header();

	int num_pages = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	for (int used_space = 80; used_space <= 80; used_space += 5) {
		int highest_lba = (int) ((double) num_pages * used_space / 100);
		printf("----------------------------------------------------------------------------------------------------------\n");
		printf("Experiment with max %d pct used space: Writing to no LBA higher than %d (out of %d total available)\n", used_space, highest_lba, num_pages);
		printf("----------------------------------------------------------------------------------------------------------\n");
		Thread* t1 = new Asynchronous_Sequential_Thread(0, highest_lba-1, 1, WRITE, 10);
		t1->add_follow_up_thread(new Asynchronous_Random_Thread(0, highest_lba-1, num_random_writes, time(NULL), WRITE, 111, 1));

		vector<Thread*> threads;
		threads.push_back(t1);

		OperatingSystem* os = new OperatingSystem(threads);
		os->run();

		csv_file << used_space << ", " << StatisticsGatherer::get_instance()->totals_csv_line();

		StateTracer::print();

		delete os;
	}

	stringstream graph_name;
    graph_name << "Overprovisioning experiment (";
    graph_name << num_random_writes;
    graph_name << " random writes)";
	csv_file.close();
    DrawGraph(
    	16, 10, "overprovisioning_experiment", csv_filename, graph_name.str(),
    	measurement_name, "units", "",
    	"d3 line marker cross\nd6 line marker circle\nd7 line marker triangle"
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
