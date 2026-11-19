#include "Claude.hpp"

using namespace geode::prelude;

namespace ai_builder {

namespace {

constexpr auto API_BASE = "https://api.anthropic.com";
constexpr auto API_VERSION = "2026-10-21";
constexpr auto KEY_SAVE_ID = "anthropic-api-key";

// What Claude is told about the editor. Kept stable (no timestamps or
// per-request data) so it can be prompt-cached. Object IDs and property
// keys were checked against the community object list (gd-info-explorer)
// and the GD level-string docs (gddocs).
constexpr auto SYSTEM_PROMPT = R"(You are a Geometry Dash (version 2.2) level designer working inside the in-game editor. The player describes a section of a level; you reply with the objects to place: gameplay, decoration and triggers.

COORDINATES
- Positions are in grid cells. One cell = one standard block (30 editor units).
- col is horizontal, increasing to the right (the direction the player travels). col 0 is the left edge of the build area.
- row is vertical. row 0 is the cell resting directly on the ground; row 1 is one block above that, and so on. Never use negative rows.
- Fractional values are allowed (e.g. row 0.5) for decoration and fine placement, but keep gameplay objects on whole cells.
- rotation is in degrees, clockwise (0, 90, 180, 270 are typical). scale is 1 for normal size.

GAMEPLAY OBJECT IDS
Blocks: 1 = standard block, 40 = half-height slab, 83 = grid square, 2 = grid top, 3 = grid outer corner, 4 = grid inner corner, 5 = grid inner, 6 = grid pillar top, 7 = grid pillar, 116 = brick square, 117 = brick top, 118 = brick outer corner, 120 = brick inner, 62 = wavy slab, 289 = slope (1x1), 291 = wide slope (2x1), 752 = grass block top, 755 = grass block centre
Hazards: 8 = spike, 39 = half-height spike, 103 = small spike, 9 = spike pit (ground hazard), 61 = wavy pit
Pads: 35 = yellow jump pad, 67 = blue gravity pad, 140 = pink jump pad
Orbs: 36 = yellow orb, 84 = blue gravity orb, 141 = pink orb
Gamemode portals: 12 = cube, 13 = ship, 47 = ball, 111 = UFO, 660 = wave, 745 = robot, 1331 = spider, 1933 = swing
Gravity portals: 10 = normal gravity, 11 = upside-down gravity
Size portals: 99 = normal size, 101 = mini
Speed changers: 200 = 0.5x, 201 = 1x, 202 = 2x, 203 = 3x, 1334 = 4x
Collectibles: 1329 = user coin

DECORATION IDS (no hitbox)
Ground decoration: 18 = large decorative spikes, 19 = medium, 20 = small, 21 = very small; 406, 407, 408, 414 = grass patches; 123 = large thorn, 125 = medium thorn, 127 = small thorn
Gears: 85 = large, 86 = medium, 87 = small, 97 = very small
Clouds: 129 = medium, 130 = large, 131 = small, 222 = large round, 223 = medium round, 224 = small round, 48 = large fading, 49 = small fading
Chains: 41 = tall chain, 106 = large wide chain, 107 = small wide chain, 110 = small chain
Pulsing shapes (pulse to the music): 50 = filled circle, 51 = circle, 52 = heart, 53 = diamond, 54 = star, 60 = music note, 132 = arrow, 148 = square, 149 = triangle
Glow: 503 = straight glow edge, 504 = outer corner glow, 1011 = large straight glow
Other: 113 = large decorative bricks, 114 = medium, 115 = small; 157 = wavy decoration; 191 = fake spike, 198 = fake half spike
Decoration usually goes behind gameplay: give it z_order -1 or lower when it overlaps blocks. Glow pieces hug block edges; rotate them to face outward.

TRIGGER IDS
899 = colour, 901 = move, 1007 = alpha, 1049 = toggle, 1006 = pulse, 1268 = spawn, 1346 = rotate, 1347 = follow, 1520 = shake, 1616 = stop

HOW TRIGGERS WORK
- A trigger fires when the player crosses its column. Put triggers at row 6 or higher so they sit out of the play area, in the column where the effect should start.
- Triggers act on groups. Give the objects to affect a group in their "groups" list, then target that group from the trigger.
- Use group numbers starting at 1 and counting up (1, 2, 3...). The mod renumbers them to free groups in the level, so don't worry about clashes.
- Trigger settings go in "props" as {key, value} pairs using the level-string keys below. Leave props empty for non-triggers.

TRIGGER PROPERTY KEYS
Common to all triggers: 10 = duration in seconds; 30 = easing (0 none, 1 ease in-out, 2 ease in, 3 ease out, 13 sine in-out, 14 sine in, 15 sine out, 16 back in-out); 85 = easing rate (usually 2); 11 = touch triggered (1 = fires when the player touches it instead of crossing its column); 62 = spawn triggered (1 = only fires when a spawn trigger activates its group); 87 = multi-trigger (1 = can fire more than once)
Colour 899: 23 = target colour channel (1000 = background, 1001 = ground, 1009 = ground 2, 1002 = line, 1004 = object, 1-999 = custom channels, which other parts of the level may already use); 7, 8, 9 = red, green, blue (0-255); 35 = opacity (0-1); 17 = blending (1/0)
Move 901: 51 = target group; 28 = x offset, 29 = y offset, both in units where 30 = one block; 58 = lock to player x, 59 = lock to player y (1/0)
Alpha 1007: 51 = target group; 35 = opacity (0-1)
Toggle 1049: 51 = target group; 56 = 1 to turn the group on, 0 to turn it off
Pulse 1006: 52 = target type (0 = colour channel, 1 = group); 51 = target channel or group; 7, 8, 9 = red, green, blue; 45 = fade in, 46 = hold, 47 = fade out (seconds)
Spawn 1268: 51 = group to activate (its spawn-triggered triggers fire); 63 = delay in seconds
Rotate 1346: 51 = target group; 71 = centre group (a single object to rotate around); 68 = degrees; 69 = full extra turns
Follow 1347: 51 = target group; 71 = group to follow; 72 = x multiplier; 73 = y multiplier
Shake 1520: 75 = strength; 84 = interval
Stop 1616: 51 = group whose running triggers to stop

OTHER OBJECT FIELDS
- groups: group numbers for the object (empty if none).
- color_channel: main colour channel for the object, or 0 to keep its default.
- z_order: draw order, 0 for default; negative draws behind, positive draws in front.

DESIGN RULES
- The level must be playable: every jump must be possible at the current speed and gamemode. A cube at 1x speed clears about 3-4 cells horizontally and 2 rows vertically per normal jump.
- Spikes sit on top of a surface: on the ground at row 0, or on a block at the row above it.
- Portals are tall (about 3 rows); centre them around the row where the player will be.
- Ship, wave, UFO and swing sections need a ceiling and a floor of blocks to define the corridor.
- Decoration must never hide a hazard or make a fake path look real, unless the player asks for that.
- Only add decoration or triggers when the player asks for them or when they clearly fit the request.
- Do not stack two objects in the exact same cell with the same ID.
- Stay within the object limit you are given (triggers and decoration count toward it).

Put a one or two sentence description of what you built in "summary".)";

matjson::Value buildSchema() {
    auto typed = [](char const* type) { return matjson::makeObject({{"type", type}}); };

    auto propSchema = matjson::makeObject({
        {"type", "object"},
        {"properties", matjson::makeObject({
            {"key", typed("integer")},
            {"value", typed("number")},
        })},
        {"required", std::vector<matjson::Value>{"key", "value"}},
        {"additionalProperties", false},
    });

    auto objectSchema = matjson::makeObject({
        {"type", "object"},
        {"properties", matjson::makeObject({
            {"id", typed("integer")},
            {"col", typed("number")},
            {"row", typed("number")},
            {"rotation", typed("number")},
            {"scale", typed("number")},
            {"flip_x", typed("boolean")},
            {"flip_y", typed("boolean")},
            {"groups", matjson::makeObject({{"type", "array"}, {"items", typed("integer")}})},
            {"color_channel", typed("integer")},
            {"z_order", typed("integer")},
            {"props", matjson::makeObject({{"type", "array"}, {"items", propSchema}})},
        })},
        {"required", std::vector<matjson::Value>{
            "id", "col", "row", "rotation", "scale", "flip_x", "flip_y",
            "groups", "color_channel", "z_order", "props"
        }},
        {"additionalProperties", false},
    });

    return matjson::makeObject({
        {"type", "object"},
        {"properties", matjson::makeObject({
            {"summary", typed("string")},
            {"objects", matjson::makeObject({
                {"type", "array"},
                {"items", objectSchema},
            })},
        })},
        {"required", std::vector<matjson::Value>{"summary", "objects"}},
        {"additionalProperties", false},
    });
}

web::WebRequest baseRequest(std::string const& key) {
    web::WebRequest req;
    req.header("x-api-key", key);
    req.header("anthropic-version", API_VERSION);
    req.header("content-type", "application/json");
    req.userAgent("ai-builder-geode-mod");
    return req;
}

// Pulls the API's own error message out of a non-2xx response.
std::string describeHttpError(web::WebResponse const& res) {
    std::string detail;
    if (auto json = res.json()) {
        detail = (*json)["error"]["message"].asString().unwrapOr("");
    }
    switch (res.code()) {
        case 401: return "Invalid API key. Re-link your account with a key from console.anthropic.com.";
        case 403: return "This API key isn't allowed to use that model. " + detail;
        case 429: return "Rate limited by the Claude API. Wait a moment and try again.";
        case 529: return "The Claude API is overloaded right now. Try again shortly.";
        default: break;
    }
    if (res.code() <= 0) {
        return "Couldn't reach api.anthropic.com. Check your internet connection.";
    }
    return fmt::format("Claude API error {}: {}", res.code(), detail.empty() ? "unknown error" : detail);
}

} // namespace

bool isAllowedPropKey(int key) {
    switch (key) {
        case 7: case 8: case 9: case 10: case 11: case 17: case 23:
        case 28: case 29: case 30: case 35: case 45: case 46: case 47:
        case 51: case 52: case 56: case 58: case 59: case 62: case 63:
        case 68: case 69: case 71: case 72: case 73: case 75: case 84:
        case 85: case 87:
            return true;
        default:
            return false;
    }
}

bool isGroupPropKey(int key) {
    return key == 51 || key == 71;
}

std::optional<std::string> getApiKey() {
    auto key = Mod::get()->getSavedValue<std::string>(KEY_SAVE_ID, "");
    if (key.empty()) return std::nullopt;
    return key;
}

void setApiKey(std::string const& key) {
    Mod::get()->setSavedValue<std::string>(KEY_SAVE_ID, key);
}

void clearApiKey() {
    Mod::get()->setSavedValue<std::string>(KEY_SAVE_ID, "");
}

web::WebFuture verifyKey(std::string const& key) {
    auto req = baseRequest(key);
    req.timeout(asp::Duration::fromSecs(20));
    return req.get(fmt::format("{}/v1/models?limit=1", API_BASE));
}

web::WebFuture requestBuild(std::string const& prompt) {
    auto mod = Mod::get();
    auto model = mod->getSettingValue<std::string>("model");
    auto effort = mod->getSettingValue<std::string>("effort");
    auto maxObjects = mod->getSettingValue<int64_t>("max-objects");

    auto body = matjson::makeObject({
        {"model", model},
        {"max_tokens", 16000},
        {"system", std::vector<matjson::Value>{
            matjson::makeObject({
                {"type", "text"},
                {"text", SYSTEM_PROMPT},
                {"cache_control", matjson::makeObject({{"type", "ephemeral"}})},
            })
        }},
        {"output_config", matjson::makeObject({
            {"effort", effort},
            {"format", matjson::makeObject({
                {"type", "json_schema"},
                {"schema", buildSchema()},
            })},
        })},
        {"messages", std::vector<matjson::Value>{
            matjson::makeObject({
                {"role", "user"},
                {"content", fmt::format(
                    "Object limit: {}.\n\nBuild this:\n{}", maxObjects, prompt
                )},
            })
        }},
    });

    auto req = baseRequest(getApiKey().value_or(""));

    // Opus 5.5 and Sonnet 5.5 can decline a request via safety classifiers;
    // server-side fallbacks retry it on another model instead of failing.
    if (model != "claude-haiku-5-5") {
        req.header("anthropic-beta", "server-side-fallback-2026-07-01");
        body.set("fallbacks", "default");
    }

    req.bodyJSON(body);
    req.timeout(asp::Duration::fromSecs(300));
    return req.post(fmt::format("{}/v1/messages", API_BASE));
}

Result<> parseVerifyResponse(web::WebResponse const& res) {
    if (res.ok()) return Ok();
    return Err(describeHttpError(res));
}

Result<BuildPlan> parseBuildResponse(web::WebResponse const& res) {
    if (!res.ok()) return Err(describeHttpError(res));

    GEODE_UNWRAP_INTO(auto json, res.json().mapErr([](auto const& e) {
        return fmt::format("Couldn't read the API response: {}", e);
    }));

    auto stopReason = json["stop_reason"].asString().unwrapOr("");
    if (stopReason == "refusal") {
        return Err("Claude declined this request. Try rewording it.");
    }
    if (stopReason == "max_tokens") {
        return Err("The build was too big and got cut off. Ask for a smaller section or lower the object limit.");
    }

    // With thinking on, content holds thinking blocks followed by one text
    // block. The text block is the JSON that matches the schema.
    std::string text;
    if (auto content = json["content"].asArray()) {
        for (auto const& block : *content) {
            if (block["type"].asString().unwrapOr("") == "text") {
                text += block["text"].asString().unwrapOr("");
            }
        }
    }
    if (text.empty()) return Err("Claude returned an empty response.");

    GEODE_UNWRAP_INTO(auto planJson, matjson::parse(text).mapErr([](auto const& e) {
        return fmt::format("Claude's answer wasn't valid JSON: {}", e.message);
    }));

    BuildPlan plan;
    plan.summary = planJson["summary"].asString().unwrapOr("");

    if (auto objects = planJson["objects"].asArray()) {
        for (auto const& o : *objects) {
            PlacedObject obj;
            obj.id = static_cast<int>(o["id"].asInt().unwrapOr(1));
            obj.col = o["col"].asDouble().unwrapOr(0);
            obj.row = std::max(0.0, o["row"].asDouble().unwrapOr(0));
            obj.rotation = o["rotation"].asDouble().unwrapOr(0);
            obj.scale = o["scale"].asDouble().unwrapOr(1);
            obj.flipX = o["flip_x"].asBool().unwrapOr(false);
            obj.flipY = o["flip_y"].asBool().unwrapOr(false);
            obj.colorChannel = static_cast<int>(o["color_channel"].asInt().unwrapOr(0));
            obj.zOrder = static_cast<int>(o["z_order"].asInt().unwrapOr(0));
            if (auto groups = o["groups"].asArray()) {
                for (auto const& g : *groups) {
                    auto id = static_cast<int>(g.asInt().unwrapOr(0));
                    if (id > 0 && obj.groups.size() < 10) obj.groups.push_back(id);
                }
            }
            if (auto props = o["props"].asArray()) {
                for (auto const& prop : *props) {
                    auto key = static_cast<int>(prop["key"].asInt().unwrapOr(0));
                    if (!isAllowedPropKey(key)) continue;
                    obj.props.emplace_back(key, prop["value"].asDouble().unwrapOr(0));
                }
            }
            if (obj.id <= 0 || obj.scale <= 0) continue;
            plan.objects.push_back(obj);
        }
    }

    if (plan.objects.empty()) return Err("Claude didn't place any objects. Try a more specific description.");
    return Ok(std::move(plan));
}

} // namespace ai_builder
