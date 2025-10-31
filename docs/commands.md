# Adding Custom Commands

This guide explains how to add new console commands to the engine.

## Command System Overview

Commands are registered with the CommandRegistry and executed when typed in the console.

## Creating a New Command

### Step 1: Define the Command Handler

Command handlers have this signature:
```cpp
void myCommand(const std::vector<std::string>& args) {
    // args[0] is the command name
    // args[1], args[2], etc. are the arguments
}
```

### Step 2: Register the Command

In `console_commands.cpp`, add your command to the `registerAll()` function:

```cpp
registry.registerCommand(
    "mycommand",                    // Command name
    "Description of my command",    // Description
    "mycommand <arg1> <arg2>",     // Usage string
    myCommandHandler               // Handler function
);
```

### Step 2b: Add Tab Completion for Arguments (Optional)

You can provide argument suggestions for tab completion:

```cpp
registry.registerCommand(
    "mycommand",
    "Description of my command",
    "mycommand <option>",
    myCommandHandler,
    {"option1", "option2", "option3"}  // Argument suggestions
);
```

Now users can type `mycommand ` and press Tab to cycle through the options!

### Step 3: Access Game State

Use the static pointers in ConsoleCommands:
- `s_console` - The console object (for output)
- `s_player` - The player object
- `s_world` - The world object

### Example Command

```cpp
static void cmdGiveItem(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: give <item>", ConsoleMessageType::WARNING);
        return;
    }

    std::string item = args[1];
    // Give the item to the player...

    s_console->addMessage("Gave item: " + item, ConsoleMessageType::INFO);
}
```

## Creating Console Variables

ConVars are settings that persist and can be changed from console.

### Step 1: Add to DebugState (or create your own manager)

```cpp
class GameState {
public:
    ConVar<float> gravity;

    GameState() : gravity("gravity", "Gravity strength", 9.8f, FCVAR_ARCHIVE) {}
};
```

### Step 2: Use the ConVar

```cpp
float currentGravity = GameState::instance().gravity.getValue();
GameState::instance().gravity.setValue(12.0f);
```

ConVars can be changed from console:
- `set gravity 15.0`
- `get gravity`

## ConVar Flags

- `FCVAR_NONE` - No special behavior
- `FCVAR_ARCHIVE` - Save to config.ini
- `FCVAR_CHEAT` - Only works in cheat mode (not implemented yet)
- `FCVAR_NOTIFY` - Print to console when changed
