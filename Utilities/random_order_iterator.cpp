/*
 * Reliable_Random_Number_Gen.cpp
 *
 *  Created on: Sep 9, 2012
 *      Author: niv
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"
using namespace ssd;

#include <algorithm> // random_shuffle

MTRand_int32 Random_Order_Iterator::random_number_generator = MTRand_int32(23652362462462462);

void Random_Order_Iterator::shuffle (std::vector<int> & order)
{
    int i = 0;
    while (i < order.size())
    {
       int k = random_number_generator() % order.size();
       int temp = order[i];
       order[i] = order[k];
       order[k] = temp;
       i++;
    }
}

vector<int> Random_Order_Iterator::get_iterator(int needed_length) {
	vector<int> order;
	for (int i = 0; i < needed_length; i++) {
		order.push_back(i);
	}
	shuffle(order);
	return order;
}
