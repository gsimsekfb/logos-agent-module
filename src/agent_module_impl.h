#pragma once

#include "logos_module_context.h"
#include <string>

/**
 * AgentModuleImpl — Autonomous AI Agent for Logos Core (LP-0008)
 *
 * Public methods below become the module's RPC API automatically.
 * Everything is Qt-free: use std::string, not QString.
 * The Qt plugin glue (Q_OBJECT, Q_INVOKABLE, etc.) is generated
 * from this header by logos-module-builder.
 */
class AgentModuleImpl : public LogosModuleContext
{
public:
    // ── Agent Lifecycle ─────────────────────────────────────────
    
    /// Returns the agent's current status as a JSON string.
    /// Contains: identity, running skills, uptime, version.
    std::string getStatus();

    /// Initialize the agent with a JSON configuration string.
    /// Config includes: owner_identity, ai_backend, spending_thresholds.
    /// Returns JSON: { "success": bool, "agent_id": string }
    std::string initialize(const std::string& configJson);

    // ── Owner Channel ───────────────────────────────────────────

    /// Process an incoming owner command (natural language or structured).
    /// Returns JSON: { "response": string, "actions_taken": [...] }
    std::string processOwnerCommand(const std::string& command);

    // ── Skills ──────────────────────────────────────────────────

    /// List available skills and their status.
    /// Returns JSON array of { "name", "description", "enabled" }.
    std::string listSkills();

    /// Execute a skill by name with the given JSON parameters.
    /// Returns JSON: { "success": bool, "result": ... }
    std::string executeSkill(const std::string& skillName,
                             const std::string& paramsJson);

logos_events:
    /// Emitted when the agent produces a response for the owner.
    void ownerResponse(const std::string& responseJson);

    /// Emitted when the agent completes a skill execution.
    void skillCompleted(const std::string& skillName,
                        const std::string& resultJson);

    /// Emitted when the agent's status changes (started, stopped, error).
    void statusChanged(const std::string& statusJson);

protected:
    /// Called once the module context (persistence paths, dependencies) is ready.
    void onContextReady() override;

private:
    bool m_initialized = false;
    std::string m_agentId;
    std::string m_ownerIdentity;
};
