#pragma once

// The virtual-model fulfillment pipeline (tasks 7.4/7.5/7.6, P5).
//
// Flow: the game declares virtual models and builds its world against their
// placeholders -> exportManifest() writes everything an external agent needs
// to produce the real assets -> the agent delivers baked .mgemesh files named
// by asset id -> fulfillFromDirectory() swaps them in under the SAME ids —
// zero scene changes.

#include <string>

#include "mge/framework/asset_registry.h"

namespace mge {

// A virtual model must be buildable by an agent that has never seen the
// game: non-empty description, sane proportions.
bool validateVirtualModelDesc(const VirtualModelDesc& desc, std::string* error = nullptr);

// Writes a JSON manifest of every unfulfilled virtual model in the registry:
// id (hex), name, proportions, shape, collidable, description/style/
// materials/features. Returns the number of entries written (0 with an
// existing file meaning "nothing to build").
size_t exportManifest(const AssetRegistry& assets, const char* jsonPath);

// Ingests agent deliveries: for each unfulfilled virtual model, looks for
// `<directory>/<id-hex-16>.mgemesh` and fulfills it. Returns how many were
// fulfilled. Proportion mismatches warn (registry-side) but do not reject.
size_t fulfillFromDirectory(AssetRegistry& assets, const char* directory);

// The delivery filename an agent must use for a given asset id.
std::string fulfillmentFileName(AssetId id);

}  // namespace mge
