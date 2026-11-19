#include "Claude.hpp"

using namespace geode::prelude;

namespace ai_builder {

namespace {

constexpr auto API_BASE = "https://api.anthropic.com";
constexpr auto API_VERSION = "2026-10-21";
constexpr auto KEY_SAVE_ID = "anthropic-api-key";

// What Claude is told about the editor. Kept stable (no timestamps or
// per-request data) so it can be prompt-cached.
constexpr auto SYSTEM_PROMPT = R"(You are a Geometry Dash (version 2.2) level designer working inside the in-game editor. The player describes a section of a level; you reply with the objects to place.

COORDINATES
- Positions are in grid cells. One cell = one standard block (30 editor units).
- col is horizontal, increasing to the right (the direction the player travels). col 0 is the left edge of the build area.
- row is vertical. row 0 is the cell resting directly on the ground; row 1 is one block above that, and so on. Never use negative rows.
- Fractional values are allowed (e.g. row 0.5) but keep most objects on whole cells.
- rotation is in degrees, clockwise (0, 90, 180, 270 are typical). scale is 1 for normal size.

OBJECT IDS (use only these unless you are certain of another ID)
Blocks: 1 = standard square block, 40 = half-height slab
Hazards: 8 = spike, 39 = small (half-height) spike
Pads: 35 = yellow jump pad, 67 = blue gravity pad, 140 = pink jump pad
Orbs: 36 = yellow orb, 84 = blue gravity orb, 141 = pink orb
Gamemode portals: 12 = cube, 13 = ship, 47 = ball, 111 = UFO, 660 = wave, 745 = robot, 1331 = spider, 1933 = swing
Gravity portals: 10 = normal gravity, 11 = upside-down gravity
Size portals: 99 = normal size, 101 = mini
Speed changers: 200 = 0.5x, 201 = 1x, 202 = 2x, 203 = 3x, 1334 = 4x
Collectibles: 1329 = user coin

DESIGN RULES
- The level must be playable: every jump must be possible at the current speed and gamemode. A cube at 1x speed clears about 3-4 cells horizontally and 2 rows vertically per normal jump.
- Spikes (8, 39) sit on top of a surface: on the ground at row 0, or on a block at the row above it.
- Portals are tall (about 3 rows); centre them around the row where the player will be.
- Ship, wave, UFO and swing sections need a ceiling and a floor of blocks to define the corridor.
- Do not stack two objects in the exact same cell with the same ID.
- Prefer clean, readable gameplay over clutter. Stay within the object limit you are given.

Put a one or two sentence description of what you built in "summary".)";

matjson::Value buildSchema() {
    auto objectSchema = matjson::makeObject({
        {"type", "object"},
        {"properties", matjson::makeObject({
            {"id", matjson::makeObject({{"type", "integer"}})},
            {"col", matjson::makeObject({{"type", "number"}})},
            {"row", matjson::makeObject({{"type", "number"}})},
            {"rotation", matjson::makeObject({{"type", "number"}})},
            {"scale", matjson::makeObject({{"type", "number"}})},
            {"flip_x", matjson::makeObject({{"type", "boolean"}})},
            {"flip_y", matjson::makeObject({{"type", "boolean"}})},
        })},
        {"required", std::vector<matjson::Value>{
            "id", "col", "row", "rotation", "scale", "flip_x", "flip_y"
        }},
        {"additionalProperties", false},
    });

    return matjson::makeObject({
        {"type", "object"},
        {"properties", matjson::makeObject({
            {"summary", matjson::makeObject({{"type", "string"}})},
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
            if (obj.id <= 0 || obj.scale <= 0) continue;
            plan.objects.push_back(obj);
        }
    }

    if (plan.objects.empty()) return Err("Claude didn't place any objects. Try a more specific description.");
    return Ok(std::move(plan));
}

} // namespace ai_builder
