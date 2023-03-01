#include "trigger/Tee.hpp"
#include "trigger/TriggerCandidate_serialization.hpp"

namespace dunedaq {
namespace trigger {

using TCTee = Tee<triggeralgs::TriggerCandidate>;

}
}

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TCTee)
