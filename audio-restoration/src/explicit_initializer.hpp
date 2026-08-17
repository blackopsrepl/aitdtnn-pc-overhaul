#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <utility>

namespace aitd4 {

// Serializes explicit initialization and makes both success and failure
// terminal. A failed, partially initialized hook must never be retried inside
// the same process.
class ExplicitInitializer {
public:
    template <typename Initialize>
    DWORD run(Initialize&& initialize) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if (state_ != State::not_started) {
            const DWORD result = state_ == State::succeeded ? 1u : 0u;
            ReleaseSRWLockExclusive(&lock_);
            return result;
        }

        bool succeeded = false;
        try {
            succeeded = static_cast<bool>(std::forward<Initialize>(initialize)());
        } catch (...) {
            succeeded = false;
        }
        state_ = succeeded ? State::succeeded : State::failed;
        ReleaseSRWLockExclusive(&lock_);
        return succeeded ? 1u : 0u;
    }

private:
    enum class State { not_started, succeeded, failed };

    SRWLOCK lock_ = SRWLOCK_INIT;
    State state_{State::not_started};
};

} // namespace aitd4
