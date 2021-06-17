#include "TriggerZipper.hpp"
#include "trigger/TPSet.hpp"
#include "appfwk/DAQModuleHelper.hpp"

namespace dunedaq::trigger {
    using TPZipper = TriggerZipper<TPSet>;
}
DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPZipper)
