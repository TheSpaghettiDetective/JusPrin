#pragma once

// The machine side of signing in to a JusPrin account (CloudAccount.hpp):
// OrcaSlicer's OAuthJob and loopback server run on a job worker of their
// own, the system browser opens the authorize page, Orca's Http client asks
// the server, and OpenSSL makes the PKCE values. Called, never edited: no
// OrcaSlicer file changes for this.

#include "CloudAccount.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

// Fresh dependencies for one CloudSignIn; each owns its worker.
CloudAccountDeps make_cloud_account_deps();

} // namespace Slic3r::GUI::JusPrin::Agent
