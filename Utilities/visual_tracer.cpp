/*
 * ssd_visual_tracer.cpp
 *
 *  Created on: Jun 7, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

vector<vector<vector<char> > > VisualTracer::trace = vector<vector<vector<char> > >(SSD_SIZE, std::vector<std::vector<char> >(PACKAGE_SIZE, std::vector<char>(0) ));
string VisualTracer::file_name = "";
bool VisualTracer::write_to_file = false;
long VisualTracer::amount_written_to_file = 0;

void VisualTracer::init() {
	trace = vector<vector<vector<char> > >(SSD_SIZE, std::vector<std::vector<char> >(PACKAGE_SIZE, std::vector<char>(0) ));
	write_to_file = false;
}

void VisualTracer::init(string folder){
	trace = vector<vector<vector<char> > >(SSD_SIZE, std::vector<std::vector<char> >(PACKAGE_SIZE, std::vector<char>(0) ));
	write_to_file = true;
	amount_written_to_file = 0;

	string name = "trace.txt";
	file_name = folder + name;

	if (write_to_file) {
		ofstream file(file_name.c_str());
		file << "";
		file.close();
	}
}

vector<char> get_int_as_char_vector(int num) {
	char buffer [10];
	int n = sprintf (buffer, "%d", num);
	vector<char> vec(n);
	for (int i = 0; i < n; i++) {
		vec[i] = buffer[i];
	}
	return vec;
}

void mark_as_gc_or_app(bool gc, bool app, vector<vector<char> >& symbols) {
	if (app) {
		vector<char> gc_symbol(3);
		gc_symbol[0] = 'A';
		gc_symbol[1] = 'P';
		gc_symbol[2] = 'P';
		symbols.push_back(gc_symbol);
	}
	if (gc) {
		vector<char> gc_symbol(2);
		gc_symbol[0] = 'G';
		gc_symbol[1] = 'C';
		symbols.push_back(gc_symbol);
	}
}

void VisualTracer::register_completed_event(Event& event) {
	//write_to_file = true;
	if (event.get_event_type() == TRIM || !write_to_file) {
		return;
	}
	Address add = event.get_address();

	int i = event.get_current_time() - event.get_execution_time() - trace[add.package][add.die].size() - amount_written_to_file;
	write(add.package, add.die, ' ', i);

	event_type type = event.get_event_type();
	if (type == WRITE) {
		write(add.package, add.die, 't', 2 * BUS_CTRL_DELAY + BUS_DATA_DELAY);
		vector<vector<char> > symbols;
		vector<char> logical_address = get_int_as_char_vector(event.get_id());
		symbols.push_back(logical_address);
		vector<char> wait_time = get_int_as_char_vector(floor(event.get_bus_wait_time()));
		symbols.push_back(wait_time);
		mark_as_gc_or_app(event.is_garbage_collection_op(), event.is_original_application_io(), symbols);
		write_with_id(add.package, add.die, 'w', PAGE_WRITE_DELAY - 1, symbols);
	} else if (type == READ_COMMAND) {
		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		//write(add.package, add.die, 'r', PAGE_READ_DELAY - 1);
		vector<vector<char> > symbols;
		vector<char> logical_address = get_int_as_char_vector(event.get_logical_address());
		symbols.push_back(logical_address);
		vector<char> wait_time = get_int_as_char_vector(floor(event.get_bus_wait_time()));
		symbols.push_back(wait_time);
		if (event.is_flexible_read()) {
			vector<char> gc_symbol(2);
			gc_symbol[0] = 'F';
			gc_symbol[1] = 'X';
			symbols.push_back(gc_symbol);
		}
		mark_as_gc_or_app(event.is_garbage_collection_op(), event.is_original_application_io(), symbols);
		write_with_id(add.package, add.die, 'r', PAGE_READ_DELAY - 1, symbols);
	} else if (type == READ_TRANSFER) {
		//write(add.package, add.die, 't', BUS_CTRL_DELAY + BUS_DATA_DELAY - 1);
		/*vector<vector<char> > symbols;
		vector<char> logical_address = get_int_as_char_vector(event.get_application_io_id());
		symbols.push_back(logical_address);
		mark_as_gc_or_app(event.is_garbage_collection_op(), event.is_original_application_io(), symbols);
		write_with_id(add.package, add.die, 't', BUS_CTRL_DELAY + BUS_DATA_DELAY - 1, symbols);*/
		write(add.package, add.die, 't', BUS_CTRL_DELAY + BUS_DATA_DELAY - 1);
	} else if (type == ERASE) {
		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		/*vector<vector<char> > symbols;
		vector<char> logical_address = get_int_as_char_vector(event.get_id());
		symbols.push_back(logical_address);
		write_with_id(add.package, add.die, 'e', BLOCK_ERASE_DELAY - 1, symbols);*/
		write(add.package, add.die, 'e', BLOCK_ERASE_DELAY - 1);
	} else if (type == COPY_BACK) {
		vector<vector<char> > symbols;
		vector<char> logical_address = get_int_as_char_vector(event.get_id());
		symbols.push_back(logical_address);
		if (event.is_garbage_collection_op()) {
			vector<char> gc_symbol(2);
			gc_symbol[0] = 'C';
			gc_symbol[1] = 'B';
			symbols.push_back(gc_symbol);
		}

		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		write(add.package, add.die, 'r', PAGE_READ_DELAY);
		write(add.package, add.die, 't', BUS_CTRL_DELAY);
		write_with_id(add.package, add.die, 'w', PAGE_WRITE_DELAY - 1, symbols);
	}
	trace[add.package][add.die].push_back('|');
	/*if (trace[add.package][add.die].size() > 1000) {

	}*/

	if (trace[add.package][add.die].size() > 100000) {
		//print_horizontally(10000);
		//Utilization_Meter::print();
		if (write_to_file) {
			//write_file();
		} else {
			//trim_from_start(100000 / 2);
		}
	}

}


void VisualTracer::write(int package, int die, char symbol, int length) {
	for (int i = 0; i < length; i++) {
		trace[package][die].push_back(symbol);
	}
}



void VisualTracer::write_with_id(int package, int die, char symbol, int length, vector<vector<char> > symbols) {

	uint length_remaining = length - 1;

	trace[package][die].push_back(symbol);

	for (uint i = 0; i < symbols.size() && symbols[i].size() < length_remaining; i++) {
		for (uint j = 0; j < symbols[i].size(); j++) {
			trace[package][die].push_back(symbols[i][j]);
		}
		trace[package][die].push_back(symbol);
		length_remaining -= symbols[i].size() + 1;
	}

	for (uint i = 0; i < length_remaining; i++) {
		trace[package][die].push_back(symbol);
	}
}

void VisualTracer::print_horizontally(int last_how_many_characters) {
	int starting_point;
	if (last_how_many_characters == UNDEFINED) {
		starting_point = 0;
	} else if (trace[0][0].size() < last_how_many_characters) {
		starting_point = 0;
	} else {
		starting_point = trace[0][0].size() - last_how_many_characters;
	}
	printf("\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			printf("p%d d%d :", i, j);
			for (uint c = starting_point; c < trace[i][j].size(); c++) {
				printf("%c", trace[i][j][c]);
			}
			printf("\n");
		}
	}
}


void VisualTracer::print_horizontally_with_breaks(ulong cursor) {
	printf("\n");
	int chars_to_write_each_time = 150;
	while (cursor < trace[0][0].size()) {
		for (uint i = 0; i < SSD_SIZE; i++) {
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				printf("p%d d%d :", i, j);
				for (uint c = 0; c < chars_to_write_each_time; c++) {
					if (trace[i][j].size() > cursor + c) {
						printf("%c", trace[i][j][cursor + c]);
					}
				}
				printf("\n");
			}
		}
		printf("\nLine %d\n", cursor / chars_to_write_each_time);
		cursor += chars_to_write_each_time;
	}
}

void VisualTracer::write_file() {
	int chars_per_line = 1000;
	// figure out how much to write
	int min_size = INFINITE;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			int size = trace[i][j].size();
			if (size < min_size) {
				min_size = size;
			}
		}
	}
	int how_many_full_lines_can_we_write = min_size / chars_per_line;
	int num_chars_to_write = how_many_full_lines_can_we_write * chars_per_line;

	 ofstream file;
	 file.open(file_name.c_str(), ios::app);
	 string trace_str = get_as_string(0, num_chars_to_write, chars_per_line);
	 file << trace_str << endl;
	 file.close();

	 trim_from_start(num_chars_to_write);
	amount_written_to_file += num_chars_to_write;
}

void VisualTracer::trim_from_start(int num_characters_from_start) {
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			vector<char>& vec = trace[i][j];
			int to_remove = min(num_characters_from_start, (int)vec.size());
			vec.erase(vec.begin(), vec.begin() + to_remove);
		}
	}
	amount_written_to_file += num_characters_from_start;
}

string VisualTracer::get_as_string(ulong cursor, ulong max, int chars_per_line) {
	stringstream ss;
	while (cursor < trace[0][0].size() && cursor < max) {
		for (uint i = 0; i < SSD_SIZE; i++) {
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				ss << "p" << i << " d" << j << " :";
				//printf("p%d d%d :", i, j);
				for (uint c = 0; c < chars_per_line; c++) {
					if (trace[i][j].size() > cursor + c) {
						//printf("%c", trace[i][j][cursor + c]);
						ss << trace[i][j][cursor + c];
					}
				}
				ss << "\n";
				//printf("\n");
			}
		}
		ss << "\nLine " << cursor / chars_per_line << "\n";
		//printf("\nLine %d\n", cursor / chars_to_write_each_time);
		cursor += chars_per_line;
	}
	return ss.str();
}

void VisualTracer::print_horizontally_with_breaks_last(long how_many_chars) {
	print_horizontally_with_breaks(trace[0][0].size() - how_many_chars);
}

void VisualTracer::print_vertically() {
	printf("\n");
	for (uint c = 0; c < trace[0][0].size(); c++) {
		for (uint i = 0; i < SSD_SIZE; i++) {
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				if (trace[i][j].size() > c) {
					printf("%c", trace[i][j][c]);
				}
			}
		}
		printf("\n");
	}

}

