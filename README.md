# cyberpunk-animgraph-bridge

RED4ext plugin that exposes a TCP API for streaming live animation control state from Cyberpunk 2077 (AnimFeatures, vars, wrappers, custom params, external events).

Used by [cyberpunk-animgraph-editor](https://github.com/Oxion/cyberpunk-animgraph-editor) to drive its sim from the running game.

## Requirements

- Cyberpunk 2077
- [RED4ext](https://github.com/WopsS/RED4ext)

## Install

Extract a release zip into the game root so you get:

```text
Cyberpunk 2077/red4ext/plugins/AnimgraphBridge/animgraph-bridge.dll
Cyberpunk 2077/red4ext/plugins/AnimgraphBridge/config.json
```

Or from a local build:

```bash
xmake f -m release
xmake
xmake deploy   # needs gamedir in env.local.lua
```

## Config

`config.json` next to the DLL:

```json
{
  "host": "127.0.0.1",
  "port": 29770,
  "prewatch": {
    "player": true,
    "entityIds": [],
    "stream": "player"
  }
}
```

- `host` / `port` — TCP listen address
- `prewatch.player` / `entityIds` — entities to accumulate
- `prewatch.stream` — `"player"` or an entity id for the live TCP stream

## Usage

1. Start the game with the plugin loaded.
2. Connect a TCP client to `host:port` (e.g. [cyberpunk-animgraph-editor](https://github.com/Oxion/cyberpunk-animgraph-editor)).
3. On connect the plugin sends one `snapshot`, then live ops until disconnect.

## Protocol

TCP **server** in the plugin. Framing: **NDJSON** — one JSON object per line, `\n`-terminated. Server → client only (no request API). One client at a time; a new accept replaces the previous and resends `snapshot`.

### Value types

Typed fields use `{ "type": "<t>", "value": <json> }`:

| `type` | `value` |
|--------|---------|
| `float` | number |
| `int` | number |
| `bool` | `true` / `false` |
| `vector` / `quat` | `{ "x", "y", "z", "w" }` |
| `cname` / `string` | JSON string |
| `enum` | number |

### `snapshot`

Sent once after connect (full board for the streamed entity).

```json
{
  "op": "snapshot",
  "entity": "1234567890",
  "source": "instance",
  "features": {
    "Locomotion": { "speed": { "type": "float", "value": 1.25 } }
  },
  "vars": {
    "isMoving": { "type": "bool", "value": true }
  },
  "wrappers": {
    "upperBody": 0.5
  }
}
```

- `entity` — streamed entity id, or `"none"`
- `source` — `"instance"` | `"shadow"` | `"mixed"` (instance buffer vs hook shadow)
- `wrappers` — name → weight number

### `setFeature`

Changed AnimFeature properties since last publish.

```json
{
  "op": "setFeature",
  "feature": "Locomotion",
  "props": [{ "name": "speed", "type": "float", "value": 1.25 }]
}
```

### `setVar`

Changed graph input variable.

```json
{ "op": "setVar", "name": "isMoving", "type": "bool", "value": true }
```

### `setWrapper`

Changed anim wrapper weight.

```json
{ "op": "setWrapper", "name": "upperBody", "value": 0.5 }
```

### `tick`

One game `Update` pulse (sim clock + external events this frame).

```json
{
  "op": "tick",
  "dt": 0.0166667,
  "rawDt": 0.0166667,
  "externalEvents": ["OnLand"]
}
```

### `serviceEvent`

`PushCustomParam` payloads the plugin can serialize. `event.kind` selects shape:

`MixerSlotParams`

```json
{
  "op": "serviceEvent",
  "event": {
    "kind": "MixerSlotParams",
    "isFacial": false,
    "enableInertiaBlend": true,
    "normalAnimParamsList": [],
    "additiveAnimParamsList": [],
    "overrideAnimParamsList": []
  }
}
```

Anim param entries: `animationName`, `previousTime`, `currentTime`, `loops`, `weight`, `bodyPartMask`, `convertToAdditive`, `extractTrajectoryForBlend`, `useDynamicAnimSets`, `muteAnimEvents`.

`LookAtParams_Add`

```json
{
  "op": "serviceEvent",
  "event": {
    "kind": "LookAtParams_Add",
    "ref": { "id": 0, "part": "Head" },
    "request": { "transitionSpeed": 1.0, "limits": {}, "additionalParts": [] }
  }
}
```

`LookAtParams_Remove` — `ref`, `hasOutTransition`, `outTransitionSpeed`

`LookAtParams_UpdatePositions` — `infos[]` of `{ "ref", "targetWs": {x,y,z,w} }`

Unsupported custom param types are dropped (no line).

## Build

```bash
git clone --recurse-submodules <repo-url>
# or after clone: git submodule update --init --recursive

xmake f -m release
xmake
```

Package release zip:

```powershell
.\tools\dist\package-mod.ps1 -Build
```

→ `build/dist/AnimgraphBridge-<version>.zip`

Optional game path (deploy only): copy `env.local.example.lua` → `env.local.lua`.

## License

[GPL-3.0](LICENSE)
