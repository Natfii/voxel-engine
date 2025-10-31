#include "input_manager.h"

InputManager& InputManager::instance() {
    static InputManager instance;
    return instance;
}
