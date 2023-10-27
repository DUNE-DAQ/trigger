#include "trigger/Tee.hpp"
#include "trigger/TriggerActivity_serialization.hpp"

namespace dunedaq {
namespace trigger {

using TCTee = Tee<triggeralgs::TriggerActivity>;

}
}

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TCTee)
