#ifndef SWAY_IPC_HPP
#define SWAY_IPC_HPP

#include "bar.hpp"

// Fetch workspace state from the sway/i3 IPC socket ($SWAYSOCK or $I3SOCK)
// and update the bar. Returns 0 on success, -1 on failure.
int sway_ipc_update_workspaces(Bar *bar);

// Fetch the focused window from the sway tree and update the bar's active
// window fields. Returns 0 on success, -1 on failure.
int sway_ipc_update_active_window(Bar *bar);

// Close the persistent IPC connection (call on shutdown).
void sway_ipc_disconnect();

#endif