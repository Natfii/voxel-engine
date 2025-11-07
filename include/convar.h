/**
 * @file convar.h
 * @brief Console variable (ConVar) system for runtime configuration
 *
 * This system provides console variables (cvars) that can be modified at runtime
 * via the in-game console. ConVars support multiple types (bool, int, float, string)
 * and can be persisted to config files.
 *
 * Features:
 * - Type-safe variables with automatic conversion
 * - Automatic registration with global registry
 * - Optional persistence (FCVAR_ARCHIVE saves to config.ini)
 * - Console notifications on value change (FCVAR_NOTIFY)
 * - Convenient operator overloads for easy access
 *
 * Usage Example:
 * @code
 * // 1. Declare a ConVar member variable
 * class MyClass {
 * public:
 *     // Archived bool (saved to config.ini)
 *     ConVar<bool> enableFeature;
 *
 *     // Non-archived float with notification
 *     ConVar<float> sensitivity;
 *
 *     MyClass() :
 *         enableFeature("my_feature", "Enable my feature", false, FCVAR_ARCHIVE),
 *         sensitivity("my_sensitivity", "Mouse sensitivity", 1.0f, FCVAR_NOTIFY)
 *     {}
 * };
 *
 * // 2. Access ConVar values
 * if (myClass.enableFeature.getValue()) {
 *     // Feature is enabled
 * }
 *
 * // Or use implicit conversion
 * if (myClass.enableFeature) {
 *     // Also works!
 * }
 *
 * // 3. Modify ConVar values programmatically
 * myClass.sensitivity.setValue(2.5f);
 *
 * // Or use assignment operator
 * myClass.sensitivity = 2.5f;
 *
 * // 4. Modify via console command (user types in console)
 * // "my_sensitivity 3.0"
 * // This calls: ConVarManager::findConVar("my_sensitivity")->setValueFromString("3.0")
 *
 * // 5. List all ConVars (console command "cvarlist")
 * for (const auto& [name, cvar] : ConVarManager::instance().getConVars()) {
 *     std::cout << name << " = " << cvar->getValueAsString() << std::endl;
 * }
 *
 * // 6. Persistence (automatic on shutdown if FCVAR_ARCHIVE is set)
 * ConVarManager::instance().saveToConfig();  // Saves to config.ini
 * ConVarManager::instance().loadFromConfig();  // Loads from config.ini
 * @endcode
 *
 * Common ConVar Flags:
 * - FCVAR_NONE: No special behavior
 * - FCVAR_ARCHIVE: Save to config.ini for persistence across sessions
 * - FCVAR_NOTIFY: Print notification when value changes
 * - FCVAR_CHEAT: Only works in cheat mode (not yet implemented)
 * - Combine with bitwise OR: FCVAR_ARCHIVE | FCVAR_NOTIFY
 *
 * Created by original author
 */

#pragma once

#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <iostream>

/**
 * @brief Flags controlling ConVar behavior
 */
enum ConVarFlags {
    FCVAR_NONE = 0,
    FCVAR_ARCHIVE = (1 << 0),  // Save to config file
    FCVAR_CHEAT = (1 << 1),    // Only works in cheat mode
    FCVAR_NOTIFY = (1 << 2)    // Print to console when changed
};

// Base class for console variables
class ConVarBase {
public:
    ConVarBase(const std::string& name, const std::string& description, int flags);
    virtual ~ConVarBase() = default;

    const std::string& getName() const { return m_name; }
    const std::string& getDescription() const { return m_description; }
    int getFlags() const { return m_flags; }

    virtual std::string getValueAsString() const = 0;
    virtual void setValueFromString(const std::string& value) = 0;

protected:
    std::string m_name;
    std::string m_description;
    int m_flags;
};

// Templated ConVar for different types
template<typename T>
class ConVar : public ConVarBase {
public:
    ConVar(const std::string& name, const std::string& description,
           T defaultValue, int flags = FCVAR_NONE);

    T getValue() const { return m_value; }
    void setValue(T value);

    std::string getValueAsString() const override;
    void setValueFromString(const std::string& value) override;

    // Operator overloads for convenience
    operator T() const { return m_value; }
    ConVar& operator=(T value) { setValue(value); return *this; }

private:
    T m_value;
};

// ConVar registry/manager
class ConVarManager {
public:
    static ConVarManager& instance();

    void registerConVar(ConVarBase* convar);
    ConVarBase* findConVar(const std::string& name);

    const std::map<std::string, ConVarBase*>& getConVars() const { return m_convars; }

    // Save all FCVAR_ARCHIVE convars to config
    void saveToConfig();
    // Load convars from config
    void loadFromConfig();

private:
    ConVarManager() = default;
    ConVarManager(const ConVarManager&) = delete;
    ConVarManager& operator=(const ConVarManager&) = delete;

    std::map<std::string, ConVarBase*> m_convars;
};

// Template implementations
template<typename T>
ConVar<T>::ConVar(const std::string& name, const std::string& description,
                  T defaultValue, int flags)
    : ConVarBase(name, description, flags), m_value(defaultValue) {
    ConVarManager::instance().registerConVar(this);
}

template<typename T>
void ConVar<T>::setValue(T value) {
    m_value = value;
    if (m_flags & FCVAR_NOTIFY) {
        std::cout << m_name << " = " << getValueAsString() << std::endl;
    }
}

template<typename T>
std::string ConVar<T>::getValueAsString() const {
    std::ostringstream oss;
    oss << m_value;
    return oss.str();
}

template<typename T>
void ConVar<T>::setValueFromString(const std::string& value) {
    std::istringstream iss(value);
    T temp;
    if (iss >> temp) {
        setValue(temp);
    }
}

// Specialization for bool (accept "true"/"false" and "1"/"0")
template<>
inline void ConVar<bool>::setValueFromString(const std::string& value) {
    if (value == "true" || value == "1") {
        setValue(true);
    } else if (value == "false" || value == "0") {
        setValue(false);
    }
}

template<>
inline std::string ConVar<bool>::getValueAsString() const {
    return m_value ? "true" : "false";
}

// Specialization for string
template<>
inline void ConVar<std::string>::setValueFromString(const std::string& value) {
    setValue(value);
}
