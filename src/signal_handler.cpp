#include "signal_handler.h"

#include <signal.h>

namespace {

volatile sig_atomic_t g_stop_requested = 0;

void on_signal(int signal_number) {
    (void)signal_number;
    g_stop_requested = 1;
}

} // namespace

bool install_signal_handlers() {
    struct sigaction stop_action {};
    stop_action.sa_handler = on_signal;
    sigemptyset(&stop_action.sa_mask);

    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);

    return sigaction(SIGINT, &stop_action, nullptr) == 0 &&
           sigaction(SIGTERM, &stop_action, nullptr) == 0 &&
           sigaction(SIGPIPE, &ignore_action, nullptr) == 0;
}

bool stop_requested() { return g_stop_requested != 0; }

void request_stop() { g_stop_requested = 1; }
