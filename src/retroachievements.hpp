#pragma once

#include <string>

namespace nfshp2::achievements {
// All client calls run on the emulation thread, except start/shutdown, which
// run before/after that thread. HTTP responses are delivered by frame/idle.
bool start(const std::string& disc_path);
void frame();
void idle();
void state_loaded();
void shutdown();
std::string take_notice();
}
