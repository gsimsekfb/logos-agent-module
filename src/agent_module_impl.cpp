#include "agent_module_impl.h"
#include <sstream>
#include <chrono>
#include <ctime>

// ── Lifecycle ───────────────────────────────────────────────────

void AgentModuleImpl::onContextReady()
{
    // Module context is now populated:
    // - instancePersistencePath() for local state
    // - instanceId() for unique identity
    // - modulePath() for module assets
}

std::string AgentModuleImpl::getStatus()
{
    std::ostringstream oss;
    oss << "{"
        << "\"initialized\": " << (m_initialized ? "true" : "false") << ", "
        << "\"agent_id\": \"" << m_agentId << "\", "
        << "\"version\": \"0.1.0\", "
        << "\"skills\": [\"echo\"]"
        << "}";
    return oss.str();
}

std::string AgentModuleImpl::initialize(const std::string& configJson)
{
    // MVP: just mark as initialized and generate an ID
    // TODO: Parse configJson for owner_identity, ai_backend, thresholds
    m_initialized = true;
    m_agentId = "agent-" + instanceId();

    std::string status = getStatus();
    statusChanged(status);

    std::ostringstream oss;
    oss << "{"
        << "\"success\": true, "
        << "\"agent_id\": \"" << m_agentId << "\""
        << "}";
    return oss.str();
}

// ── Owner Channel ───────────────────────────────────────────────

std::string AgentModuleImpl::processOwnerCommand(const std::string& command)
{
    if (!m_initialized) {
        return "{\"error\": \"Agent not initialized. Call initialize() first.\"}";
    }

    // MVP: echo-based skill — demonstrates the full owner→agent→response flow
    std::string response;
    if (command.find("status") != std::string::npos) {
        response = getStatus();
    } else if (command.find("skills") != std::string::npos) {
        response = listSkills();
    } else {
        // Default: echo the command back
        response = executeSkill("echo", command);
    }

    // Emit event so the owner channel can deliver it
    ownerResponse(response);

    std::ostringstream oss;
    oss << "{"
        << "\"response\": " << response << ", "
        << "\"actions_taken\": [\"processed_command\"]"
        << "}";
    return oss.str();
}

// ── Skills ──────────────────────────────────────────────────────

std::string AgentModuleImpl::listSkills()
{
    return "[{\"name\": \"echo\", \"description\": \"Echoes input back\", \"enabled\": true}]";
}

std::string AgentModuleImpl::executeSkill(const std::string& skillName,
                                           const std::string& paramsJson)
{
    if (!m_initialized) {
        return "{\"success\": false, \"error\": \"Agent not initialized\"}";
    }

    std::string result;

    if (skillName == "echo") {
        // MVP: simple echo skill
        std::ostringstream oss;
        oss << "{\"success\": true, \"result\": \"Echo: " << paramsJson << "\"}";
        result = oss.str();
    } else {
        result = "{\"success\": false, \"error\": \"Unknown skill: " + skillName + "\"}";
    }

    // Emit skill completed event
    skillCompleted(skillName, result);

    return result;
}
