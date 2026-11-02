#include "agent_module_impl.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

// logos::LpClient — Qt-free client for calling any module by name over
// the logos-protocol C ABI. No LIDL needed.
#include "logos_lp_client.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

// ── Hex conversion helpers ──────────────────────────────────────

/// Converts a raw byte buffer to a lowercase hex string.
/// Accepts unsigned char* + length because OpenSSL APIs output in that format.
std::string AgentModuleImpl::toHex(const unsigned char *data, int len) {
  std::ostringstream oss;
  for (int i = 0; i < len; ++i)
    oss << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(data[i]);
  return oss.str();
}

// ── Owner Channel internals ─────────────────────────────────────

/// Holds the LpClient for Logos Messaging and the active subscription.
/// Forward-declared in the header so the header stays Qt/protocol-free.
struct AgentModuleImpl::OwnerChannel {
  logos::LpClient client;             // talks to chat_module
  logos::LpSubscription subscription; // incoming message events
  std::string conversationId;         // active conversation ID
  bool connected = false;             // whether channel is established

  OwnerChannel(const std::string& origin)
      : client("chat_module", origin) {}
};

/// Timeout for wallet RPCs. LpClient::invoke() defaults to the protocol's
/// 20s timeout when 0 is passed — far too long when the wallet module is
/// simply not loaded (standalone logoscore), and it equals the CLI's own
/// call timeout, so the caller gives up before our fallback can answer.
static constexpr int kWalletTimeoutMs = 3000;

/// Holds the LpClient for querying the LEZ wallet module.
/// Forward-declared in the header so the header stays Qt/protocol-free.
struct AgentModuleImpl::WalletClient {
  logos::LpClient client;

  WalletClient(const std::string& origin)
      : client("logos_execution_zone", origin) {}
};

// Destructor must be here (not in header) so the raw pointers
// see the complete types.
AgentModuleImpl::~AgentModuleImpl() {
  delete m_ownerChannel;
  delete m_walletClient;
}

// ── Identity persistence ────────────────────────────────────────

/// Returns the full filesystem path for the identity JSON file,
/// or empty string if no persistence directory is available.
///
/// `/agent_identity.json` — persists the agent's Ed25519 keypair
/// (public + private key) across restarts.
static std::string identityFilePath(const std::string &persistDir) {
  if (persistDir.empty())
    return "";
  return persistDir + "/agent_identity.json";
}

/// Writes the keypair to disk so the agent keeps the same identity
/// across restarts.
/// TODO: MVP stores private key in plaintext. Production must encrypt it
///       (e.g., with a passphrase or OS keyring).
bool AgentModuleImpl::saveIdentity() {
  std::string path = identityFilePath(instancePersistencePath());
  if (path.empty())
    return false;

  std::ofstream f(path);
  if (!f.is_open())
    return false;

  f << "{\n"
    << "  \"public_key\": \"" << m_publicKeyHex << "\",\n"
    << "  \"private_key\": \"" << m_privateKeyHex << "\"\n"
    << "}\n";
  return f.good();
}

/// Loads an existing keypair from disk.
/// Returns false if the file doesn't exist or can't be parsed.
bool AgentModuleImpl::loadIdentity() {
  std::string path = identityFilePath(instancePersistencePath());
  if (path.empty())
    return false;

  std::ifstream f(path);
  if (!f.is_open())
    return false;

  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  // Simple JSON value extraction — finds "key": "value" pairs.
  auto extractValue = [&](const std::string &key) -> std::string {
    auto pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos)
      return "";
    pos = content.find("\"", pos + key.size() + 2);
    if (pos == std::string::npos)
      return "";
    pos++; // skip opening quote
    auto end = content.find("\"", pos);
    if (end == std::string::npos)
      return "";
    return content.substr(pos, end - pos);
  };

  m_publicKeyHex = extractValue("public_key");
  m_privateKeyHex = extractValue("private_key");

  if (m_publicKeyHex.empty() || m_privateKeyHex.empty())
    return false;

  m_identityLoaded = true;
  return true;
}

/// Tries to load an existing identity from disk; if none exists,
/// generates a new Ed25519 keypair and persists it.
/// NOTE: MVP uses Ed25519 as a placeholder. Will migrate to LEZ NPK/ISK
///       via the wallet module when it supports the universal LIDL interface.
bool AgentModuleImpl::loadOrGenerateIdentity() {
  // Try loading existing identity first
  if (loadIdentity()) {
    return true;
  }

  // Generate new Ed25519 keypair using OpenSSL
  EVP_PKEY *pkey_pair = nullptr;
    // OpenSSL generic keypair handle (holds the Ed25519 keypair)
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (!ctx)
    return false;

  bool ok = false;
  if (EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &pkey_pair) > 0) {
    // Extract raw public key (32 bytes for Ed25519)
    unsigned char pubKey[32];
    size_t pubLen = 32;
    EVP_PKEY_get_raw_public_key(pkey_pair, pubKey, &pubLen);
    m_publicKeyHex = toHex(pubKey, static_cast<int>(pubLen));

    // Extract raw private key/seed (32 bytes for Ed25519)
    unsigned char privKey[32];
    size_t privLen = 32;
    EVP_PKEY_get_raw_private_key(pkey_pair, privKey, &privLen);
    m_privateKeyHex = toHex(privKey, static_cast<int>(privLen));
    // Zero out private key material from stack memory so the sensitive key
    // material doesn't linger in memory where it could be read by a
    // crash dump, debugger, or memory-scraping attack.
    memset(privKey, 0, sizeof(privKey));

    m_identityLoaded = true;
    ok = saveIdentity();
  }

  EVP_PKEY_free(pkey_pair);
  EVP_PKEY_CTX_free(ctx);
  return ok;
}

// ── Lifecycle ───────────────────────────────────────────────────

void AgentModuleImpl::onContextReady() {
  loadOrGenerateIdentity();
  registerDefaultSkills();
}

std::string AgentModuleImpl::getStatus() {
  // Build skills array from registry
  std::ostringstream skillsStr;
  skillsStr << "[";
  for (size_t i = 0; i < m_skills.size(); ++i) {
    if (i > 0) skillsStr << ", ";
    skillsStr << "\"" << m_skills[i].name << "\"";
  }
  skillsStr << "]";

  std::ostringstream oss;
  oss << "{"
      << "\"initialized\": " << (m_initialized ? "true" : "false") << ", "
      << "\"identity_loaded\": " << (m_identityLoaded ? "true" : "false")
      << ", "
      << "\"owner_channel_connected\": "
      << (m_ownerChannel && m_ownerChannel->connected ? "true" : "false")
      << ", "
      << "\"agent_id\": \"" << m_agentId << "\", "
      << "\"public_key\": \"" << m_publicKeyHex << "\", "
      << "\"version\": \"0.1.0\", "
      << "\"skills\": " << skillsStr.str()
      << "}";
  return oss.str();
}

std::string AgentModuleImpl::initialize(const std::string &configJson) {
  if (!m_identityLoaded) {
    loadOrGenerateIdentity();
  }

  m_initialized = true;
  // short ID from first 8 bytes of public key
  m_agentId = m_publicKeyHex.substr(0, 16);

  std::string status = getStatus();
  statusChanged(status);

  std::ostringstream oss;
  oss << "{"
      << "\"success\": true, "
      << "\"agent_id\": \"" << m_agentId << "\", "
      << "\"public_key\": \"" << m_publicKeyHex << "\""
      << "}";
  return oss.str();
}

// ── Identity ────────────────────────────────────────────────────

std::string AgentModuleImpl::getIdentity() {
  std::ostringstream oss;
  oss << "{"
      << "\"loaded\": " << (m_identityLoaded ? "true" : "false") << ", "
      << "\"public_key\": \"" << m_publicKeyHex << "\", "
      << "\"agent_id\": \"" << m_agentId << "\", "
      << "\"messaging_address\": \"agent-" << m_publicKeyHex.substr(0, 16)
      << "\""
      << "}";
  return oss.str();
}

// ── Owner Channel ───────────────────────────────────────────────

std::string AgentModuleImpl::setupOwnerChannel(
    const std::string& ownerIntroBundle) {
  if (!m_initialized) {
    return "{\"success\": false, \"error\": "
           "\"Agent not initialized. Call initialize() first.\"}";
  }

  // Create or reuse the LpClient for the chat module
  if (!m_ownerChannel) {
    m_ownerChannel = new OwnerChannel(moduleName());
  }

  // Create a private (E2E encrypted) conversation with the owner.
  // Args: [introBundleStr, contentHex]
  // contentHex is the first message in hex-encoded form.
  std::string welcomeHex;
  {
    std::string welcome = "Agent " + m_agentId + " connected";
    std::ostringstream oss;
    for (unsigned char c : welcome)
      oss << std::hex << std::setfill('0') << std::setw(2)
          << static_cast<int>(c);
    welcomeHex = oss.str();
  }

  logos::CallError err;
  auto result = m_ownerChannel->client.invoke(
      "newPrivateConversation",
      nlohmann::json::array({ownerIntroBundle, welcomeHex}),
      &err
  );

  if (!err.code.empty()) {
    std::ostringstream oss;
    oss << "{\"success\": false, \"error\": \"" << err.message << "\"}";
    return oss.str();
  }

  // Extract conversation ID from the result
  if (result.is_object() && result.contains("id")) {
    m_ownerChannel->conversationId = result["id"].get<std::string>();
  } else if (result.is_string()) {
    m_ownerChannel->conversationId = result.get<std::string>();
  }

  // Subscribe to incoming messages on the chat module.
  // The chat module emits events when new messages arrive.
  m_ownerChannel->subscription = m_ownerChannel->client.subscribe(
      "messageReceived", 
      [this](nlohmann::json payload) {
        if (payload.is_array() && !payload.empty()) {
          onChatMessage(payload.dump());
        }
      }
  );

  m_ownerChannel->connected = true;

  std::ostringstream oss;
  oss << "{\"success\": true, \"conversation_id\": \""
      << m_ownerChannel->conversationId << "\"}";
  return oss.str();
}

std::string AgentModuleImpl::sendToOwner(const std::string& message) {

  // If the messaging channel is set up, send via Logos Messaging
  if (m_ownerChannel && m_ownerChannel->connected &&
      !m_ownerChannel->conversationId.empty()
    ) {

    // Hex-encode the message for the chat module API
    std::ostringstream hexOss;
    for (unsigned char c : message)
      hexOss << std::hex << std::setfill('0') << std::setw(2)
             << static_cast<int>(c);

    logos::CallError err;
    m_ownerChannel->client.invoke(
      "sendMessage",
      nlohmann::json::array(
          {m_ownerChannel->conversationId, hexOss.str()}),
      &err
    );

    if (err.code.empty()) {
      return "{\"success\": true, \"via\": \"messaging\"}";
    }
    // Fall through to event-based delivery on failure
  }

  // Fallback: emit ownerResponse event for direct RPC subscribers
  ownerResponse(message);
  return "{\"success\": true, \"via\": \"event\"}";
}

std::string AgentModuleImpl::getOwnerChannelStatus() {
  std::ostringstream oss;
  oss << "{\"connected\": "
      << (m_ownerChannel && m_ownerChannel->connected ? "true" : "false")
      << ", \"conversation_id\": \"";
  if (m_ownerChannel)
    oss << m_ownerChannel->conversationId;
  oss << "\"}";
  return oss.str();
}

void AgentModuleImpl::onChatMessage(const std::string& messageJson) {
  // Route incoming chat messages through the command processor.
  // In production, we'd parse the JSON to extract the actual message
  // content from the chat module's event payload.
  std::string response = processOwnerCommand(messageJson);

  // Send the response back via the messaging channel
  sendToOwner(response);
}

std::string AgentModuleImpl::processOwnerCommand(const std::string& command) {
  if (!m_initialized) {
    return "{\"error\": \"Agent not initialized. Call initialize() first.\"}";
  }

  std::string response;
  if (command.find("status") != std::string::npos) {
    response = getStatus();
  } else if (command.find("skills") != std::string::npos) {
    response = listSkills();
  } else if (command.find("identity") != std::string::npos) {
    response = getIdentity();
  } else if (command.find("channel") != std::string::npos) {
    response = getOwnerChannelStatus();
  } else if (command.find("balance") != std::string::npos) {
    response = executeSkill("wallet.balance", "{}");
  } else {
    response = executeSkill("echo", command);
  }

  ownerResponse(response);

  std::ostringstream oss;
  oss << "{\"command\": \"" << command << "\", "
      << "\"response\": " << response << ", "
      << "\"actions_taken\": [\"processed_command\"]"
      << "}";
  return oss.str();
}

// ── Skill Registry ─────────────────────────────────────────────

void AgentModuleImpl::registerDefaultSkills() {
  // echo — simple echo skill (existed since 1.3)
  m_skills.push_back({"echo", "Echoes input back", true});
  m_skillHandlers["echo"] = [this](const std::string& p) {
    return handleEcho(p);
  };

  // meta.status — reports agent state: identity, channel, balance, skills
  m_skills.push_back({"meta.status",
      "Reports agent state: identity, balance, storage, active tasks", true});
  m_skillHandlers["meta.status"] = [this](const std::string& p) {
    return handleMetaStatus(p);
  };

  // wallet.balance — queries shielded balance from the LEZ wallet module
  m_skills.push_back({"wallet.balance",
      "Returns the agent's shielded LEZ token balance", true});
  m_skillHandlers["wallet.balance"] = [this](const std::string& p) {
    return handleWalletBalance(p);
  };
}

// ── Skill Handlers ─────────────────────────────────────────────

std::string AgentModuleImpl::handleEcho(const std::string& paramsJson) {
  std::ostringstream oss;
  oss << "{\"success\": true, \"result\": \"Echo: " << paramsJson << "\"}";
  return oss.str();
}

/// meta.status — enriched agent status per LP-0008 spec.
/// Includes: balance (from wallet if available), storage usage, active tasks.
std::string AgentModuleImpl::handleMetaStatus(const std::string& /*paramsJson*/) {
  // Query wallet balance if possible (graceful fallback)
  std::string balanceStr = "\"unavailable\"";
  std::string balanceResult = handleWalletBalance("{}");
  // Extract balance from the wallet.balance result
  auto pos = balanceResult.find("\"balance\": ");
  if (pos != std::string::npos) {
    auto valStart = pos + 11; // length of "balance": 
    auto valEnd = balanceResult.find_first_of(",}", valStart);
    if (valEnd != std::string::npos) {
      balanceStr = balanceResult.substr(valStart, valEnd - valStart);
    }
  }

  // Build skills array from registry
  std::ostringstream skillsStr;
  skillsStr << "[";
  for (size_t i = 0; i < m_skills.size(); ++i) {
    if (i > 0) skillsStr << ", ";
    skillsStr << "{\"name\": \"" << m_skills[i].name
              << "\", \"enabled\": "
              << (m_skills[i].enabled ? "true" : "false") << "}";
  }
  skillsStr << "]";

  std::ostringstream oss;
  oss << "{\"success\": true, \"result\": {"
      << "\"initialized\": " << (m_initialized ? "true" : "false") << ", "
      << "\"identity_loaded\": " << (m_identityLoaded ? "true" : "false")
      << ", "
      << "\"owner_channel_connected\": "
      << (m_ownerChannel && m_ownerChannel->connected ? "true" : "false")
      << ", "
      << "\"agent_id\": \"" << m_agentId << "\", "
      << "\"public_key\": \"" << m_publicKeyHex << "\", "
      << "\"version\": \"0.1.0\", "
      << "\"balance\": " << balanceStr << ", "
      << "\"storage_usage\": \"unavailable\", "
      << "\"active_tasks\": 0, "
      << "\"skills\": " << skillsStr.str()
      << "}}";
  return oss.str();
}

/// wallet.balance — queries the LEZ wallet module via LpClient.
/// Returns the shielded balance, or "unavailable" if the wallet module
/// is not running (e.g., standalone logoscore testing).
std::string AgentModuleImpl::handleWalletBalance(
    const std::string& /*paramsJson*/) {
  // Lazily create the wallet client on first use
  if (!m_walletClient) {
    m_walletClient = new WalletClient(moduleName());
  }

  // Call the wallet module's list_accounts first to find our account,
  // then get_balance. For MVP, we try get_balance with a default account.
  logos::CallError err;
  auto result = m_walletClient->client.invoke(
      "list_accounts", nlohmann::json::array(), &err, kWalletTimeoutMs);

  if (!err.code.empty()) {
    // Wallet module not available (expected in standalone logoscore)
    return "{\"success\": true, \"balance\": \"unavailable\", "
           "\"reason\": \"wallet module not connected\"}";
  }

  // Try to get balance from the first account
  std::string accountId;
  if (result.is_array() && !result.empty()) {
    if (result[0].is_object() && result[0].contains("id")) {
      accountId = result[0]["id"].get<std::string>();
    } else if (result[0].is_string()) {
      accountId = result[0].get<std::string>();
    }
  }

  if (accountId.empty()) {
    return "{\"success\": true, \"balance\": 0, "
           "\"reason\": \"no accounts found\"}";
  }

  // get_balance(accountId, isPublic)
  // isPublic = false for shielded balance
  logos::CallError balErr;
  auto balResult = m_walletClient->client.invoke(
      "get_balance",
      nlohmann::json::array({accountId, false}),
      &balErr, kWalletTimeoutMs);

  if (!balErr.code.empty()) {
    return "{\"success\": true, \"balance\": \"unavailable\", "
           "\"reason\": \"balance query failed\"}";
  }

  std::ostringstream oss;
  oss << "{\"success\": true, \"balance\": " << balResult.dump()
      << ", \"account_id\": \"" << accountId << "\"}";
  return oss.str();
}

// ── Skills (public API) ────────────────────────────────────────

std::string AgentModuleImpl::listSkills() {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < m_skills.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << "{\"name\": \"" << m_skills[i].name
        << "\", \"description\": \"" << m_skills[i].description
        << "\", \"enabled\": " << (m_skills[i].enabled ? "true" : "false")
        << "}";
  }
  oss << "]";
  return oss.str();
}

std::string AgentModuleImpl::executeSkill(const std::string &skillName,
                                          const std::string &paramsJson) {
  // Skill execution does not require initialize() — meta.status and
  // wallet.balance should be callable for diagnostics even before init.

  auto it = m_skillHandlers.find(skillName);
  if (it == m_skillHandlers.end()) {
    std::string result =
        "{\"success\": false, \"error\": \"Unknown skill: " + skillName + "\"}";
    skillCompleted(skillName, result);
    return result;
  }

  std::string result = it->second(paramsJson);
  skillCompleted(skillName, result);
  return result;
}
