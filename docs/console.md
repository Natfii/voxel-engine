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

### wireframe
Toggle wireframe rendering mode (shows mesh wireframes instead of solid blocks).
- Example: `wireframe`

### lighting
Toggle the voxel lighting system on/off. Regenerate chunks (move around) to see the effect.
- Example: `lighting`

### debug <option>
Toggle various debug rendering modes.
- Example: `debug render` - Toggle chunk rendering debug information
- Example: `debug drawfps` - Toggle FPS counter in the corner
- Example: `debug targetinfo` - Toggle target information display (shows details about the block you're looking at)

Available options: render, drawfps, targetinfo

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

### skytime <0-1>
Set the time of day (0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset).
- Example: `skytime 0.5` (set to noon)
- Example: `skytime 0.75` (set to sunset)
- Example: `skytime` (show current time)

### timespeed <value>
Set the time progression speed (0=paused, 1=normal, higher=faster).
- Example: `timespeed 0` (pause time)
- Example: `timespeed 1` (normal speed, 20 minute cycle)
- Example: `timespeed 10` (10x faster, 2 minute cycle)
- Example: `timespeed` (show current speed)

**Tab completion:** Press Tab after `timespeed ` to cycle through common values.

See [sky_system.md](sky_system.md) for detailed sky system documentation.

### spawnstructure <name>
Spawn a structure at the targeted ground position. Look at the ground where you want to place the structure and run this command.
- Example: `spawnstructure house`
- Example: `spawnstructure` (list available structures)

**Tab completion:** Press Tab after `spawnstructure ` to cycle through available structure names.

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
