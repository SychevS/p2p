#include "log.h"

INITIALIZE_EASYLOGGINGPP

namespace net {

void InitLogger() {
  static bool inited = false;

  if (!inited) {
    using namespace el;

    Configurations conf;
    conf.set(Level::Global, ConfigurationType::Enabled, "true");
    conf.set(Level::Global, ConfigurationType::ToFile, "true");
    conf.set(Level::Global, ConfigurationType::ToStandardOutput, "false");
    conf.set(Level::Global, ConfigurationType::Filename, "debug.log");

    conf.set(Level::Info, ConfigurationType::ToStandardOutput, "true");
    conf.set(Level::Error, ConfigurationType::ToStandardOutput, "true");
    conf.set(Level::Fatal, ConfigurationType::ToStandardOutput, "true");

    Loggers::reconfigureLogger("default", conf);

    inited = true;
  }
}
} // namespace net
