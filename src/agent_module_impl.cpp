#include "agent_module_impl.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

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

void AgentModuleImpl::onContextReady() { loadOrGenerateIdentity(); }

std::string AgentModuleImpl::getStatus() {
  std::ostringstream oss;
  oss << "{"
      << "\"initialized\": " << (m_initialized ? "true" : "false") << ", "
      << "\"identity_loaded\": " << (m_identityLoaded ? "true" : "false")
      << ", "
      << "\"agent_id\": \"" << m_agentId << "\", "
      << "\"public_key\": \"" << m_publicKeyHex << "\", "
      << "\"version\": \"0.1.0\", "
      << "\"skills\": [\"echo\"]"
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

std::string AgentModuleImpl::processOwnerCommand(const std::string &command) {
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
  } else {
    response = executeSkill("echo", command);
  }

  ownerResponse(response);

  std::ostringstream oss;
  oss << "{"
      << "\"command\": \"" << command << "\", "
      << "\"response\": " << response << ", "
      << "\"actions_taken\": [\"processed_command\"]"
      << "}";
  return oss.str();
}

// ── Skills ──────────────────────────────────────────────────────

std::string AgentModuleImpl::listSkills() {
  return "[{\"name\": \"echo\", \"description\": \"Echoes input back\", "
         "\"enabled\": true}]";
}

std::string AgentModuleImpl::executeSkill(const std::string &skillName,
                                          const std::string &paramsJson) {
  if (!m_initialized) {
    return "{\"success\": false, \"error\": \"Agent not initialized\"}";
  }

  std::string result;

  if (skillName == "echo") {
    std::ostringstream oss;
    oss << "{\"success\": true, \"result\": \"Echo: " << paramsJson << "\"}";
    result = oss.str();
  } else {
    result =
        "{\"success\": false, \"error\": \"Unknown skill: " + skillName + "\"}";
  }

  skillCompleted(skillName, result);
  return result;
}
