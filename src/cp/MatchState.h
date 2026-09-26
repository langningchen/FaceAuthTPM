// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <mutex>
#include <string>

namespace faceauth {

// Shared, one-shot "face recognition confirmed this SID" token.
//
// FaceProvider owns the single instance for a logon session and Arm()s it
// when RunFaceSensor() returns a match. Every FaceCredential tile holds a
// shared_ptr to the SAME instance and must call ConsumeIfMatches(mySid)
// before it is allowed to decrypt/submit a credential. This is what makes
// tile selection alone (with no successful camera match) insufficient to
// authenticate: GetSerialization() must not proceed on cooldown/TPM checks
// alone, it must first spend this token.
class MatchState {
  public:
    // Marks that recognition just confirmed newSid. Resets consumed so the
    // token can be spent exactly once for this new match.
    void Arm(const std::wstring& newSid) {
        std::scoped_lock lock(mutex_);
        sid_ = newSid;
        consumed_ = false;
    }

    // Clears any pending/armed match (e.g. on SetUsageScenario, or after a
    // credential has been submitted/failed) so nothing carries over.
    void Clear() {
        std::scoped_lock lock(mutex_);
        sid_.clear();
        consumed_ = false;
    }

    // Non-consuming check used by GetCredentialCount to pick a default tile
    // / request auto-logon. Does NOT spend the token; only GetSerialization
    // (via ConsumeIfMatches) is allowed to do that.
    bool Peek(const std::wstring& candidateSid) {
        std::scoped_lock lock(mutex_);
        return !consumed_ && !sid_.empty() && sid_ == candidateSid;
    }

    // True if there is any unconsumed pending match (used to avoid kicking
    // off a redundant recognition pass while one is already armed).
    bool HasPending() {
        std::scoped_lock lock(mutex_);
        return !consumed_ && !sid_.empty();
    }

    // Spends the token: returns true exactly once, and only for the
    // credential whose SID equals the one recognition actually confirmed.
    // Every other credential, and every repeat call for the same one, gets
    // false.
    bool ConsumeIfMatches(const std::wstring& candidateSid) {
        std::scoped_lock lock(mutex_);
        if (consumed_ || sid_.empty() || sid_ != candidateSid)
            return false;
        consumed_ = true;
        return true;
    }

  private:
    std::mutex mutex_;
    std::wstring sid_;
    bool consumed_ = false;
};

} // namespace faceauth
