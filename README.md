# AI Builder (Geode mod)

A Geode mod for Geometry Dash **2.2091** (Geode SDK `6.0.0-alpha.2`). It adds a button to the level editor. You describe a section of a level, the mod sends that description to the Claude API, and it places the objects Claude returns.

Platforms: Windows, macOS, Android. iOS is left out because the editor function this mod uses (`LevelEditorLayer::createObjectsFromString`) has no iOS address in the bindings yet.

## How "linking" works
Claude.ai accounts can't be signed into from third-party apps. Instead, the mod uses an **Anthropic API key** from [console.anthropic.com](https://console.anthropic.com/settings/keys):

- The key is checked against the API (`GET /v1/models`) before it is saved.
- It is stored in the mod's saved data (`saved.json` in the Geode save folder), as **plain text** on your device.
- API usage is billed to the Anthropic Console account that owns the key. It is separate from a Claude Pro or Max subscription.

## How building works
1. `src/Claude.cpp` sends your prompt to `POST /v1/messages` with a system prompt that explains the editor's grid and the object IDs Claude may use. It also sends a JSON schema (`output_config.format`) so the reply is always a list of `{id, col, row, rotation, scale, flip_x, flip_y}` objects.
2. `src/main.cpp` turns that list into a level string, starting from the grid column in the middle of the screen, and calls `createObjectsFromString` so the build can be undone. It then checks where the first object actually landed and moves everything if the editor's coordinates don't match what the mod expected.
3. The new objects are left selected.

Settings: model (`claude-opus-5-5` by default, or Sonnet 5.5 / Haiku 5.5), effort, and the maximum number of objects per build.

## Building the mod
CI builds it on every push (`.github/workflows/build.yml`). The combined `.geode` file is in the run's **Build Output** artifact.

To build it yourself, install the [Geode CLI and SDK](https://docs.geode-sdk.org/getting-started/), then:

```bash
geode build
```

## Files
- `mod.json`: mod metadata and settings
- `src/Claude.cpp/.hpp`: API key storage, requests and response parsing
- `src/Popups.cpp/.hpp`: the account popup and the build popup
- `src/main.cpp`: editor button and object placement
