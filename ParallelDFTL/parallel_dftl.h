
#include "../ssd.h"

namespace ssd {

class parallel_dftl;
class parallel_dftl_abstract_bm;
class parallel_dftl_bm_dynamic_allocator;
class parallel_dftl_bm_wearwolf;
class parallel_dftl_die;


class parallel_dftl_die
{
public:
	enum status write(Event &event);
	enum status read(Event &event);
private:
	std::vector<Block*> active_list;
	std::vector<Block*> free_list;
	std::vector<Block*> invalid_list;

	Controller &controller;
};


}
