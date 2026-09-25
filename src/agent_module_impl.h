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
    // Must be declared here and defined in .cpp so unique_ptr<OwnerChannel>
    // sees the complete type at destruction (PIMPL pattern requirement).
    ~AgentModuleImpl();
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

    /// Set up the E2E encrypted owner channel via Logos Messaging.
    /// `ownerIntroBundle` is the owner's intro bundle from the chat module.
    /// Returns JSON: { "success": bool, "conversation_id": string }
    std::string setupOwnerChannel(const std::string& ownerIntroBundle);

    /// Process an incoming owner command (natural language or structured).
    /// Works both via direct RPC and via the Logos Messaging channel.
    /// Returns JSON: { "command": string, "response": ..., "actions_taken": [...] }
    std::string processOwnerCommand(const std::string& command);

    /// Send a message to the owner via the Logos Messaging channel.
    /// Falls back to emitting ownerResponse event if channel is not set up.
    /// Returns JSON: { "success": bool, "via": "messaging"|"event" }
    std::string sendToOwner(const std::string& message);

    /// Returns the owner channel status as JSON.
    std::string getOwnerChannelStatus();

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

    // ── Owner Channel state ──────────────────────────────────────
    // Forward-declared; defined in logos_lp_client.h, included only in .cpp
    struct OwnerChannel;
    OwnerChannel* m_ownerChannel = nullptr;

    // Internal helpers — not exported as RPC methods
    bool loadOrGenerateIdentity();
    bool saveIdentity();
    bool loadIdentity();
    std::string toHex(const unsigned char* data, int len);
    void onChatMessage(const std::string& messageJson);
};
