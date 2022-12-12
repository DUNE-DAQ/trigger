#include "trigger/TPSet.hpp"
#include "trigger/Tee.hpp"

namespace dunedaq {
namespace trigger {

using TPSetTee = Tee<TPSet>;

}
}

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPSetTee)
