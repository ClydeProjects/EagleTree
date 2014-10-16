/*
 * StatisticData.cpp
 *
 *  Created on: May 11, 2014
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;

map<string, StatisticData> StatisticData::statistics = map<string, StatisticData>();

StatisticData::~StatisticData() {
	for (auto row : data) {
		for (auto val : row) {
			delete val;
		}
	}
}

void StatisticData::init() {
	statistics.clear();
}

void StatisticData::register_statistic(string name, std::initializer_list<Number*> list) {
	vector<Number*> row;
	for (auto i : list) {
		row.push_back(i);
	}
	StatisticData& stat = statistics[name];
	stat.data.push_back(row);
}

void StatisticData::register_field_names(string name, std::initializer_list<string> list) {
	StatisticData& stat = statistics[name];
	if (stat.names.size() > 0) {
		return;
	}
	for (auto i : list) {
		stat.names.push_back(i);
	}
}

double StatisticData::get_count(string name, int column) {
	StatisticData& stat = statistics.at(name);
	return stat.data.size();
}

double StatisticData::get_sum(string name, int column) {
	StatisticData& stat = statistics.at(name);
	double sum = 0;
	for (int i = 0; i < stat.data.size(); i++) {
		sum += stat.data[i][column]->toDouble();
	}
	return sum;
}

double StatisticData::get_average(string name, int column) {
	if (statistics.count(name) == 0) {
		printf("warning, a stat with name %s does not exist here.\n", name.c_str());
		return 0;
	}
	StatisticData& stat = statistics.at(name);
	double sum = get_sum(name, column);
	double size = (double) stat.data.size();
	return sum / size;
}

double StatisticData::get_weighted_avg_of_col2_in_terms_of_col1(string name, int col1, int col2) {
	if (statistics.count(name) == 0) {
		return 0.0;
	}
	long double sum_col1 = StatisticData::get_sum(name, col1);
	long double weighted_sum = 0;
	StatisticData& stat = statistics.at(name);
	for (int i = 0; i < stat.data.size(); i++) {
		double val = stat.data[i][col1]->toDouble();
		double time_diff = i == 0 ? val : val - stat.data[i-1][col1]->toDouble();
		weighted_sum += ( stat.data[i][col1]->toDouble() * stat.data[i][col2]->toDouble() ) / sum_col1;
	}
	return weighted_sum;
}

double StatisticData::get_standard_deviation(string name, int column) {
	if (statistics.count(name) == 0) {
		return 0;
	}
	StatisticData& stat = statistics.at(name);
	double avg = get_average(name, column);
	double squared_sum = 0;
	for (int i = 0; i < stat.data.size(); i++) {
		double diff = stat.data[i][column]->toDouble() - avg;
		squared_sum += diff * diff;
	}
	return sqrt(squared_sum / stat.data.size());
}

void StatisticData::clean(string name) {
	if (statistics.count(name) == 0) {
		return;
	}
	StatisticData& stat = statistics.at(name);
	for (auto row : stat.data) {
		for (int i = 0; i < row.size(); i++) {
			delete row[i];
		}
		row.clear();
	}
	stat.data.clear();
	statistics.erase(name);
}

string StatisticData::to_csv(string name) {
	StatisticData& stat = statistics.at(name);
	stringstream ss;

	for (int i = 0; i < stat.names.size(); i++) {
		ss << "\"" << stat.names[i] << "\"";
		if (i != stat.names.size() - 1) {
			ss << ",";
		}
	}
	ss << "\n";
	for (auto row : stat.data) {
		for (int i = 0; i < row.size(); i++) {
			ss << row[i]->toString();
			if (i + 1 < row.size()) {
				ss << ", ";
			}
		}
		ss << "\n";
	}
	return ss.str();
}
