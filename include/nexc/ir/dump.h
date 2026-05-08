#pragma once

#include "nexc/ir/ir.h"

#include <iosfwd>

namespace nexc::ir {

// Print a stable, human-readable view of the typed IR.
//
// This dump is an educational/debugging format, not a serialization format. It
// is intentionally plain text so golden tests can lock down IR shape while the
// compiler is still learning to lower checked Core v0 programs.
void dumpModule(std::ostream& out, const Module& module);

} // namespace nexc::ir
