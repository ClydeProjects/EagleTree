/*
 * Reliable_Random_Number_Gen.cpp
 *
 *  Created on: Sep 9, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"
using namespace ssd;

#include <algorithm> // random_shuffle

vector<int> Random_Order_Iterator::get_iterator(int needed_length) {
	vector<int> order;
	for (int i = 0; i < needed_length; i++) {
		order.push_back(i);
	}
	random_shuffle(order.begin(), order.end());
	return order;
}
