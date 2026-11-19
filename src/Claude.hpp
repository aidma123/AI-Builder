#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ai_builder {

// One object Claude wants to place. Positions are in editor grid cells
// (30 units each), relative to the build origin: col 0 is the first column
// at the camera, row 0 is the row sitting directly on the ground.
struct PlacedObject {
    int id = 1;
    double col = 0;
    double row = 0;
    double rotation = 0;
    double scale = 1;
    bool flipX = false;
    bool flipY = false;
    // Group IDs local to this build (1, 2, 3...). They are remapped to
    // unused groups in the level before placing.
    std::vector<int> groups;
    int colorChannel = 0; // 0 = object's default colour
    int zOrder = 0;       // 0 = default
    // Extra level-string properties, mostly for triggers (key -> value).
    std::vector<std::pair<int, double>> props;
};

// Level-string keys Claude may set through `props`. Anything else is dropped
// so a confused reply can't overwrite position, ID or other core fields.
bool isAllowedPropKey(int key);
// Keys whose value is a group ID (and must be remapped like `groups`).
bool isGroupPropKey(int key);

struct BuildPlan {
    std::string summary;
    std::vector<PlacedObject> objects;
};

// The API key is how the mod "links" to Claude. It is stored in the mod's
// saved data (Geode's saved.json in the GD save folder), never in settings,
// so it doesn't show up in the settings UI or exported setting files.
std::optional<std::string> getApiKey();
void setApiKey(std::string const& key);
void clearApiKey();

// Builds the request that checks an API key works (GET /v1/models).
geode::utils::web::WebFuture verifyKey(std::string const& key);

// Builds the request that asks Claude for a build plan.
geode::utils::web::WebFuture requestBuild(std::string const& prompt);

// Turns a /v1/models response into Ok or a readable error.
geode::Result<> parseVerifyResponse(geode::utils::web::WebResponse const& res);

// Turns a /v1/messages response into a BuildPlan or a readable error.
geode::Result<BuildPlan> parseBuildResponse(geode::utils::web::WebResponse const& res);

} // namespace ai_builder
