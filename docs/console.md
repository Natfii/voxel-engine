# Console Guide

Welcome to the Voxel Engine developer console!

## Opening the Console

Press **F9** to toggle the console on/off.
Press **ESC** to close the console.

## Using Commands

Simply type a command and press Enter. Use Up/Down arrow keys to navigate command history.

Press **Tab** to autocomplete commands and arguments from suggestions. Press Tab multiple times to cycle through available suggestions.

### Tab Completion Examples

- Type `deb` and press Tab → completes to `debug`
- Type `debug ` (with space) and press Tab → cycles through `debug render`, `debug drawfps`, `debug targetinfo`
- Type `debug r` and press Tab → completes to `debug render`
- Type `help ` and press Tab → cycles through all available commands

Tab completion works for both command names and their arguments!

## Available Commands

### help [command]
Shows all available commands, or detailed help for a specific command.
- Example: `help`
- Example: `help noclip`

### noclip
Toggle noclip mode (fly through walls).
- Example: `noclip`

### debug render
Toggle chunk rendering debug information.
- Example: `debug render`

### debug drawfps
Toggle FPS counter in the corner.
- Example: `debug drawfps`

### debug targetinfo
Toggle target information display (shows details about the block you're looking at).
- Example: `debug targetinfo`

### debug culling
Toggle frustum culling statistics display (shows chunks rendered/culled).
- Example: `debug culling`

### debug collision
Toggle collision detection debug output in console (shows player position, block checks).
- Example: `debug collision`

### tp <x> <y> <z>
Teleport to specific coordinates.
- Example: `tp 0 50 0`

### clear
Clear the console output.
- Example: `clear`

### echo <message>
Print a message to the console.
- Example: `echo Hello World`

### set <name> <value>
Set a console variable (ConVar) value.
- Example: `set noclip true`

### get <name>
Get a console variable (ConVar) value.
- Example: `get noclip`

### list
List all console variables.
- Example: `list`

## Opening Documentation

You can open any .md file by typing its path:
- Example: `docs/console.md`
- Example: `docs/commands.md`

## Console Variables (ConVars)

ConVars are persistent settings that can be changed from the console.
ConVars marked with [ARCHIVE] are saved to config.ini.

Use `list` to see all available ConVars.
Use `get <name>` to see a ConVar's current value.
Use `set <name> <value>` to change a ConVar's value.
