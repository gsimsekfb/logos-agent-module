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
    std::string getStatus();

    /// Initialize the agent with a JSON configuration string.
    /// Returns JSON: { "success": bool, "agent_id": string, "public_key": string }
    std::string initialize(const std::string& configJson);

    // ── Identity ────────────────────────────────────────────────

    /// Returns the agent's public identity as a JSON string.
    /// Contains: public_key (hex), agent_id, messaging_address.
    /// NOTE: MVP uses Ed25519. Will migrate to LEZ NPK/ISK when wallet
    ///       module supports the universal LIDL interface.
    std::string getIdentity();

    // ── Owner Channel ───────────────────────────────────────────

    /// Process an incoming owner command (natural language or structured).
    /// Returns JSON: { "response": string, "actions_taken": [...] }
    std::string processOwnerCommand(const std::string& command);

    // ── Skills ──────────────────────────────────────────────────

    /// List available skills and their status.
    std::string listSkills();

    /// Execute a skill by name with the given JSON parameters.
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
    void onContextReady() override;

private:
    bool m_initialized = false;       // Whether initialize() has been called
    std::string m_agentId;            // Short hex ID derived from public key
    std::string m_ownerIdentity;      // Owner's public key

    // Ed25519 public key as hex string (32 bytes = 64 hex chars).
    // MVP placeholder — will migrate to LEZ NPK when wallet module
    // supports universal LIDL interface.
    std::string m_publicKeyHex;
    // Ed25519 private key seed as hex string (32 bytes = 64 hex chars)
    std::string m_privateKeyHex;
    // Whether keypair was loaded/generated successfully
    bool m_identityLoaded = false;

    // Internal helpers — not exported as RPC methods
    bool loadOrGenerateIdentity();
    bool saveIdentity();
    bool loadIdentity();
    std::string toHex(const unsigned char* data, int len);
};
