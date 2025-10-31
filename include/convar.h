#pragma once

#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <iostream>

// ConVar flags
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
