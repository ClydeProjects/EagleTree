/*
 * Reliable_Random_Number_Gen.cpp
 *
 *  Created on: Sep 9, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"
using namespace ssd;


Reliable_Random_Int_Generator::Reliable_Random_Int_Generator(int seed, int num_numbers_needed)
	: random_number_generator(), numbers() {
	random_number_generator.seed(seed);
	int total = 0;
	for (int i = 0; i < num_numbers_needed; i++) {
		int num = random_number_generator();
		numbers.push_back(num);
	}
}

int Reliable_Random_Int_Generator::next() {
	assert(numbers.size() > 0);
	int num = numbers.front();
	numbers.pop_front();
	return num;
}

Reliable_Random_Double_Generator::Reliable_Random_Double_Generator(int seed, int num_numbers_needed)
	: random_number_generator(), numbers() {
	random_number_generator.seed(seed);
	for (int i = 0; i < num_numbers_needed; i++) {
		double num = random_number_generator();
		numbers.push_back(num);
	}
}

double Reliable_Random_Double_Generator::next() {
	assert(numbers.size() > 0);
	double num = numbers.front();
	numbers.pop_front();
	return num;
}

