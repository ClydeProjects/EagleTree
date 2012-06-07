
#include "parallel_dftl.h"

using namespace ssd;

enum status parallel_dftl_die::read(Event &event)
{

	return controller.issue(event);
}

enum status parallel_dftl_die::write(Event &event)
{

	return FAILURE;
}
