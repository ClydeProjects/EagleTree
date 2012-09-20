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

Random_Order_Iterator::Random_Order_Iterator()
 : random_number_generator(134) {}


void Random_Order_Iterator::shuffle (std::vector<int> & order)
{
    int deckSize = order.size();
    while (deckSize > 1)
    {
       int k = random_number_generator();
       k = k %24;
       deckSize--;
       int temp = order[deckSize];
       order[deckSize] = order[k];
       order[k] = temp;
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
