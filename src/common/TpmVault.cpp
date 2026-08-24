// SPDX-License-Identifier: GPL-3.0-only
#include "common/TpmVault.h"
#include "common/Constants.h"
#include "common/Paths.h"
#include "common/Security.h"
#include <array>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <ncrypt.h>
#include <sddl.h>
#include <vector>
#include <windows.h>

#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "bcrypt.lib")

namespace faceauth {
namespace {
constexpr uint32_t kMagic = 0x31564146; // FAV1
constexpr uint32_t kVersion = 1;
#pragma pack(push, 1)
struct VaultHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t sidChars;
    uint32_t wrappedKeyBytes;
    uint32_t cipherBytes;
    unsigned char nonce[12];
    unsigned char tag[16];
};
#pragma pack(pop)

struct NcryptProvider {
    NCRYPT_PROV_HANDLE h{};
    ~NcryptProvider() {
        if (h)
            NCryptFreeObject(h);
    }
};
struct NcryptKey {
    NCRYPT_KEY_HANDLE h{};
    ~NcryptKey() {
        if (h)
            NCryptFreeObject(h);
    }
};
struct BcryptAlg {
    BCRYPT_ALG_HANDLE h{};
    ~BcryptAlg() {
        if (h)
            BCryptCloseAlgorithmProvider(h, 0);
    }
};
struct BcryptKey {
    BCRYPT_KEY_HANDLE h{};
    ~BcryptKey() {
        if (h)
            BCryptDestroyKey(h);
    }
};

std::wstring SecStatusText(SECURITY_STATUS s) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"0x%08X", static_cast<unsigned>(s));
    return buf;
}

bool OpenPlatformProvider(NcryptProvider& p, std::wstring* error) {
    SECURITY_STATUS s = NCryptOpenStorageProvider(&p.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (s != ERROR_SUCCESS) {
        if (error)
            *error = L"Microsoft Platform Crypto Provider unavailable: " + SecStatusText(s) +
                     L". Ensure TPM 2.0 is enabled.";
        return false;
    }
    return true;
}

bool SetPrivateKeySystemOnly(NCRYPT_KEY_HANDLE key) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)", SDDL_REVISION_1,
                                                              &sd, nullptr))
        return false;
    DWORD cb = GetSecurityDescriptorLength(sd);
    SECURITY_STATUS s =
        NCryptSetProperty(key, NCRYPT_SECURITY_DESCR_PROPERTY, reinterpret_cast<PBYTE>(sd), cb,
                          DACL_SECURITY_INFORMATION | NCRYPT_PERSIST_FLAG);
    LocalFree(sd);
    return s == ERROR_SUCCESS;
}

bool SavePublicKey(NCRYPT_KEY_HANDLE key, std::wstring* error) {
    DWORD bytes = 0;
    SECURITY_STATUS s =
        NCryptExportKey(key, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, nullptr, 0, &bytes, 0);
    if (s != ERROR_SUCCESS || !bytes) {
        if (error)
            *error = L"Exporting TPM public key size failed: " + SecStatusText(s);
        return false;
    }
    std::vector<unsigned char> blob(bytes);
    s = NCryptExportKey(key, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, blob.data(),
                        static_cast<DWORD>(blob.size()), &bytes, 0);
    if (s != ERROR_SUCCESS) {
        if (error)
            *error = L"Exporting TPM public key failed: " + SecStatusText(s);
        return false;
    }
    blob.resize(bytes);
    auto tmp = VaultPublicKeyPath();
    tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error)
            *error = L"Cannot save TPM public key";
        return false;
    }
    f.write(reinterpret_cast<const char*>(blob.data()), blob.size());
    f.close();
    if (!f || !ApplyAdminSystemOnlyAcl(tmp)) {
        std::filesystem::remove(tmp);
        if (error)
            *error = L"Cannot secure TPM public-key file";
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, VaultPublicKeyPath(), ec);
    if (ec) {
        std::filesystem::remove(VaultPublicKeyPath(), ec);
        ec.clear();
        std::filesystem::rename(tmp, VaultPublicKeyPath(), ec);
    }
    if (ec) {
        if (error)
            *error = L"Cannot install TPM public-key file";
        return false;
    }
    ApplyAdminSystemOnlyAcl(VaultPublicKeyPath());
    return true;
}

bool LoadPublicKey(std::vector<unsigned char>& blob, std::wstring* error) {
    std::ifstream f(VaultPublicKeyPath(), std::ios::binary | std::ios::ate);
    if (!f) {
        if (error)
            *error = L"TPM public-key file not found";
        return false;
    }
    auto n = f.tellg();
    if (n <= 0 || n > 65536) {
        if (error)
            *error = L"TPM public-key file has invalid size";
        return false;
    }
    f.seekg(0);
    blob.resize(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(blob.data()), blob.size());
    if (!f) {
        if (error)
            *error = L"Reading TPM public-key file failed";
        return false;
    }
    return true;
}

bool WrapAesKeyWithPublicKey(const std::array<unsigned char, 32>& aesKey,
                             std::vector<unsigned char>& wrapped, std::wstring* error) {
    std::vector<unsigned char> blob;
    if (!LoadPublicKey(blob, error))
        return false;
    BcryptAlg alg;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (st < 0) {
        if (error)
            *error = L"BCryptOpenAlgorithmProvider(RSA) failed";
        return false;
    }
    BcryptKey key;
    st = BCryptImportKeyPair(alg.h, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.h, blob.data(),
                             static_cast<ULONG>(blob.size()), 0);
    if (st < 0) {
        if (error)
            *error = L"BCryptImportKeyPair(TPM public key) failed";
        return false;
    }
    BCRYPT_OAEP_PADDING_INFO oaep{BCRYPT_SHA256_ALGORITHM, nullptr, 0};
    ULONG out = 0;
    st = BCryptEncrypt(key.h, const_cast<PUCHAR>(aesKey.data()), static_cast<ULONG>(aesKey.size()),
                       &oaep, nullptr, 0, nullptr, 0, &out, BCRYPT_PAD_OAEP);
    if (st < 0 || !out) {
        if (error)
            *error = L"RSA-OAEP wrap size failed";
        return false;
    }
    wrapped.resize(out);
    st = BCryptEncrypt(key.h, const_cast<PUCHAR>(aesKey.data()), static_cast<ULONG>(aesKey.size()),
                       &oaep, nullptr, 0, wrapped.data(), static_cast<ULONG>(wrapped.size()), &out,
                       BCRYPT_PAD_OAEP);
    if (st < 0) {
        if (error)
            *error = L"RSA-OAEP wrap failed";
        return false;
    }
    wrapped.resize(out);
    return true;
}

bool OpenVaultPrivateKey(NcryptProvider& p, NcryptKey& key, std::wstring* error) {
    if (!OpenPlatformProvider(p, error))
        return false;
    SECURITY_STATUS s =
        NCryptOpenKey(p.h, &key.h, kTpmKeyName, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (s != ERROR_SUCCESS) {
        if (error)
            *error = L"Opening TPM private key failed: " + SecStatusText(s) +
                     L" (expected to work only as SYSTEM).";
        return false;
    }
    return true;
}

bool AesGcmEncrypt(const std::array<unsigned char, 32>& aesKey, const std::wstring& sid,
                   const unsigned char* plaintext, DWORD plaintextBytes,
                   std::array<unsigned char, 12>& nonce, std::array<unsigned char, 16>& tag,
                   std::vector<unsigned char>& cipher, std::wstring* error) {
    if (BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        if (error)
            *error = L"Generating AES-GCM nonce failed";
        return false;
    }
    BcryptAlg alg;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (st < 0)
        return false;
    st = BCryptSetProperty(
        alg.h, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (st < 0)
        return false;
    DWORD objLen = 0, cb = 0;
    st = BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                           sizeof(objLen), &cb, 0);
    if (st < 0)
        return false;
    std::vector<unsigned char> obj(objLen);
    BcryptKey key;
    st = BCryptGenerateSymmetricKey(alg.h, &key.h, obj.data(), objLen,
                                    const_cast<PUCHAR>(aesKey.data()),
                                    static_cast<ULONG>(aesKey.size()), 0);
    if (st < 0)
        return false;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce.data();
    info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbTag = tag.data();
    info.cbTag = static_cast<ULONG>(tag.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(sid.data()));
    info.cbAuthData = static_cast<ULONG>(sid.size() * sizeof(wchar_t));
    cipher.resize(plaintextBytes);
    ULONG out = 0;
    st = BCryptEncrypt(key.h, const_cast<PUCHAR>(plaintext), plaintextBytes, &info, nullptr, 0,
                       cipher.data(), static_cast<ULONG>(cipher.size()), &out, 0);
    SecureZeroMemory(obj.data(), obj.size());
    if (st < 0) {
        if (error)
            *error = L"AES-256-GCM encryption failed";
        return false;
    }
    cipher.resize(out);
    return true;
}

bool AesGcmDecrypt(const std::array<unsigned char, 32>& aesKey, const std::wstring& sid,
                   const VaultHeader& h, const unsigned char* cipher,
                   std::vector<unsigned char>& plain, std::wstring* error) {
    BcryptAlg alg;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (st < 0)
        return false;
    st = BCryptSetProperty(
        alg.h, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (st < 0)
        return false;
    DWORD objLen = 0, cb = 0;
    st = BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                           sizeof(objLen), &cb, 0);
    if (st < 0)
        return false;
    std::vector<unsigned char> obj(objLen);
    BcryptKey key;
    st = BCryptGenerateSymmetricKey(alg.h, &key.h, obj.data(), objLen,
                                    const_cast<PUCHAR>(aesKey.data()),
                                    static_cast<ULONG>(aesKey.size()), 0);
    if (st < 0)
        return false;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(h.nonce);
    info.cbNonce = sizeof(h.nonce);
    info.pbTag = const_cast<PUCHAR>(h.tag);
    info.cbTag = sizeof(h.tag);
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(sid.data()));
    info.cbAuthData = static_cast<ULONG>(sid.size() * sizeof(wchar_t));
    plain.resize(h.cipherBytes);
    ULONG out = 0;
    st = BCryptDecrypt(key.h, const_cast<PUCHAR>(cipher), h.cipherBytes, &info, nullptr, 0,
                       plain.data(), static_cast<ULONG>(plain.size()), &out, 0);
    SecureZeroMemory(obj.data(), obj.size());
    if (st < 0) {
        if (error)
            *error = L"Credential authentication failed (AES-GCM)";
        return false;
    }
    plain.resize(out);
    return true;
}
} // namespace

SecurePassword::~SecurePassword() { SecureErase(data_); }
SecurePassword::SecurePassword(SecurePassword&& other) noexcept : data_(std::move(other.data_)) {}
SecurePassword& SecurePassword::operator=(SecurePassword&& other) noexcept {
    if (this != &other) {
        SecureErase(data_);
        data_ = std::move(other.data_);
    }
    return *this;
}

bool EnsureTpmVaultKey(std::wstring* error) {
    if (!IsRunningAsLocalSystem()) {
        if (error)
            *error = L"TPM vault bootstrap must run as LocalSystem";
        return false;
    }
    if (!EnsureDataDirectories()) {
        if (error)
            *error = L"Cannot create secure FaceAuth data directories";
        return false;
    }
    NcryptProvider p;
    if (!OpenPlatformProvider(p, error))
        return false;
    NcryptKey key;
    SECURITY_STATUS s =
        NCryptOpenKey(p.h, &key.h, kTpmKeyName, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (s != ERROR_SUCCESS) {
        s = NCryptCreatePersistedKey(p.h, &key.h, NCRYPT_RSA_ALGORITHM, kTpmKeyName, 0,
                                     NCRYPT_MACHINE_KEY_FLAG);
        if (s != ERROR_SUCCESS) {
            if (error)
                *error = L"Creating TPM vault key failed: " + SecStatusText(s);
            return false;
        }
        DWORD bits = 2048, exportPolicy = 0;
        if (NCryptSetProperty(key.h, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits),
                              sizeof(bits), 0) != ERROR_SUCCESS ||
            NCryptSetProperty(key.h, NCRYPT_EXPORT_POLICY_PROPERTY,
                              reinterpret_cast<PBYTE>(&exportPolicy), sizeof(exportPolicy),
                              0) != ERROR_SUCCESS ||
            NCryptFinalizeKey(key.h, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS) {
            if (error)
                *error = L"Finalizing non-exportable TPM RSA key failed";
            return false;
        }
    }
    // Public-key export is safe; the TPM private key remains non-exportable.
    if (!SavePublicKey(key.h, error))
        return false;
    if (!SetPrivateKeySystemOnly(key.h)) {
        std::error_code ec;
        std::filesystem::remove(VaultPublicKeyPath(), ec);
        if (error)
            *error = L"Could not restrict TPM private key to SYSTEM";
        return false;
    }
    return true;
}

bool StorePasswordForSid(const std::wstring& sid, const std::wstring& password,
                         std::wstring* error) {
    if (sid.empty() || password.empty()) {
        if (error)
            *error = L"SID/password is empty";
        return false;
    }
    if (!EnsureDataDirectories()) {
        if (error)
            *error = L"Cannot create secure FaceAuth data directories";
        return false;
    }
    if (!std::filesystem::exists(VaultPublicKeyPath())) {
        if (error)
            *error = L"TPM vault is not bootstrapped. Re-run scripts\\install.ps1.";
        return false;
    }
    std::array<unsigned char, 32> aesKey{};
    if (BCryptGenRandom(nullptr, aesKey.data(), static_cast<ULONG>(aesKey.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        if (error)
            *error = L"Generating AES key failed";
        return false;
    }
    std::vector<unsigned char> wrapped;
    if (!WrapAesKeyWithPublicKey(aesKey, wrapped, error)) {
        SecureZeroMemory(aesKey.data(), aesKey.size());
        return false;
    }
    VaultHeader h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.sidChars = static_cast<uint32_t>(sid.size());
    h.wrappedKeyBytes = static_cast<uint32_t>(wrapped.size());
    std::array<unsigned char, 12> nonce{};
    std::array<unsigned char, 16> tag{};
    std::vector<unsigned char> cipher;
    const auto* plain = reinterpret_cast<const unsigned char*>(password.c_str());
    DWORD plainBytes = static_cast<DWORD>((password.size() + 1) * sizeof(wchar_t));
    bool ok = AesGcmEncrypt(aesKey, sid, plain, plainBytes, nonce, tag, cipher, error);
    SecureZeroMemory(aesKey.data(), aesKey.size());
    if (!ok)
        return false;
    h.cipherBytes = static_cast<uint32_t>(cipher.size());
    memcpy(h.nonce, nonce.data(), nonce.size());
    memcpy(h.tag, tag.data(), tag.size());
    auto path = CredentialPath(sid), tmp = path;
    tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error)
            *error = L"Cannot create credential file";
        return false;
    }
    f.write(reinterpret_cast<const char*>(&h), sizeof(h));
    f.write(reinterpret_cast<const char*>(sid.data()), sid.size() * sizeof(wchar_t));
    f.write(reinterpret_cast<const char*>(wrapped.data()), wrapped.size());
    f.write(reinterpret_cast<const char*>(cipher.data()), cipher.size());
    f.close();
    if (!f || !ApplyAdminSystemOnlyAcl(tmp)) {
        std::filesystem::remove(tmp);
        if (error)
            *error = L"Writing/securing credential file failed";
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        if (error)
            *error = L"Replacing credential file failed";
        return false;
    }
    ApplyAdminSystemOnlyAcl(path);
    SecureErase(wrapped);
    SecureErase(cipher);
    return true;
}

bool DecryptPasswordForSid(const std::wstring& sid, SecurePassword& password, std::wstring* error) {
    password = SecurePassword{};
    if (!IsRunningAsLocalSystem()) {
        if (error)
            *error = L"TPM vault decryption is restricted to LocalSystem";
        return false;
    }
    std::ifstream f(CredentialPath(sid), std::ios::binary);
    if (!f) {
        if (error)
            *error = L"No enrolled credential for SID";
        return false;
    }
    VaultHeader h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || h.magic != kMagic || h.version != kVersion || h.sidChars > 256 ||
        h.wrappedKeyBytes > 4096 || h.cipherBytes > 8192) {
        if (error)
            *error = L"Invalid credential header";
        return false;
    }
    std::wstring fileSid(h.sidChars, L'\0');
    f.read(reinterpret_cast<char*>(fileSid.data()), fileSid.size() * sizeof(wchar_t));
    if (!f || fileSid != sid) {
        if (error)
            *error = L"Credential SID mismatch";
        return false;
    }
    std::vector<unsigned char> wrapped(h.wrappedKeyBytes), cipher(h.cipherBytes);
    f.read(reinterpret_cast<char*>(wrapped.data()), wrapped.size());
    f.read(reinterpret_cast<char*>(cipher.data()), cipher.size());
    if (!f) {
        if (error)
            *error = L"Credential file truncated";
        return false;
    }
    NcryptProvider p;
    NcryptKey key;
    if (!OpenVaultPrivateKey(p, key, error))
        return false;
    BCRYPT_OAEP_PADDING_INFO oaep{BCRYPT_SHA256_ALGORITHM, nullptr, 0};
    // The vault always wraps exactly one 32-byte AES-256 key. Do not rely on
    // the TPM KSP's NULL-output size-query behavior here; some KSPs may report
    // a provider-sized requirement rather than the eventual plaintext length.
    // Decrypt into a buffer as large as the RSA ciphertext, then validate the
    // actual plaintext length returned by NCryptDecrypt.
    std::vector<unsigned char> unwrapped(wrapped.size());
    DWORD aesBytes = 0;
    SECURITY_STATUS s = NCryptDecrypt(key.h, wrapped.data(), static_cast<DWORD>(wrapped.size()),
                                      &oaep, unwrapped.data(), static_cast<DWORD>(unwrapped.size()),
                                      &aesBytes, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    if (s != ERROR_SUCCESS) {
        if (error)
            *error = L"TPM unwrap failed: " + SecStatusText(s);
        SecureErase(unwrapped);
        return false;
    }
    if (aesBytes != 32) {
        if (error)
            *error = L"TPM unwrap returned unexpected plaintext length: " +
                     std::to_wstring(aesBytes) + L" bytes (expected 32)";
        SecureErase(unwrapped);
        return false;
    }
    std::array<unsigned char, 32> aesKey{};
    memcpy(aesKey.data(), unwrapped.data(), aesKey.size());
    SecureErase(unwrapped);
    std::vector<unsigned char> plain;
    bool ok = AesGcmDecrypt(aesKey, fileSid, h, cipher.data(), plain, error);
    SecureZeroMemory(aesKey.data(), aesKey.size());
    SecureErase(wrapped);
    SecureErase(cipher);
    if (!ok || plain.size() < sizeof(wchar_t) || plain.size() % sizeof(wchar_t) != 0) {
        SecureErase(plain);
        return false;
    }
    size_t chars = plain.size() / sizeof(wchar_t);
    password.data_.resize(chars);
    memcpy(password.data_.data(), plain.data(), plain.size());
    SecureErase(plain);
    if (password.data_.back() != L'\0') {
        if (error)
            *error = L"Decrypted credential malformed";
        password = SecurePassword{};
        return false;
    }
    return true;
}

bool CredentialExists(const std::wstring& sid) {
    std::error_code ec;
    return std::filesystem::is_regular_file(CredentialPath(sid), ec);
}
bool DeleteCredentialForSid(const std::wstring& sid) {
    std::error_code ec;
    return !std::filesystem::exists(CredentialPath(sid), ec) ||
           std::filesystem::remove(CredentialPath(sid), ec);
}
} // namespace faceauth
