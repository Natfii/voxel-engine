#include "convar.h"
#include "config.h"
#include "logger.h"

ConVarBase::ConVarBase(const std::string& name, const std::string& description, int flags)
    : m_name(name), m_description(description), m_flags(flags) {
}

ConVarManager& ConVarManager::instance() {
    static ConVarManager instance;
    return instance;
}

void ConVarManager::registerConVar(ConVarBase* convar) {
    m_convars[convar->getName()] = convar;
}

ConVarBase* ConVarManager::findConVar(const std::string& name) {
    auto it = m_convars.find(name);
    if (it != m_convars.end()) {
        return it->second;
    }
    return nullptr;
}

void ConVarManager::saveToConfig() {
    Config& config = Config::instance();

    for (const auto& pair : m_convars) {
        ConVarBase* convar = pair.second;
        if (convar->getFlags() & FCVAR_ARCHIVE) {
            // Save to [Console] section in config
            // Note: We'd need to add a setValue method to Config class
            // For now, this is a placeholder
            Logger::debug() << "Saving convar: " << convar->getName()
                           << " = " << convar->getValueAsString();
        }
    }
}

void ConVarManager::loadFromConfig() {
    Config& config = Config::instance();

    for (const auto& pair : m_convars) {
        ConVarBase* convar = pair.second;
        if (convar->getFlags() & FCVAR_ARCHIVE) {
            // Try to load from [Console] section
            std::string value = config.getString("Console", convar->getName(), "");
            if (!value.empty()) {
                convar->setValueFromString(value);
            }
        }
    }
}
