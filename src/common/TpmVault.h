// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>

namespace faceauth {
class SecurePassword {
  public:
    SecurePassword() = default;
    ~SecurePassword();
    SecurePassword(const SecurePassword&) = delete;
    SecurePassword& operator=(const SecurePassword&) = delete;
    SecurePassword(SecurePassword&& other) noexcept;
    SecurePassword& operator=(SecurePassword&& other) noexcept;
    std::vector<wchar_t>& buffer() { return data_; }
    const wchar_t* c_str() const { return data_.empty() ? L"" : data_.data(); }
    bool empty() const { return data_.empty() || data_[0] == L'\0'; }

  private:
    friend bool DecryptPasswordForSid(const std::wstring&, SecurePassword&, std::wstring*);
    std::vector<wchar_t> data_;
};

bool EnsureTpmVaultKey(std::wstring* error = nullptr);
bool StorePasswordForSid(const std::wstring& sid, const std::wstring& password,
                         std::wstring* error = nullptr);
bool DecryptPasswordForSid(const std::wstring& sid, SecurePassword& password,
                           std::wstring* error = nullptr);
bool CredentialExists(const std::wstring& sid);
bool DeleteCredentialForSid(const std::wstring& sid);
} // namespace faceauth
