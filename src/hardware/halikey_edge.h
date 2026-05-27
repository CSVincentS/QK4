#ifndef HALIKEY_EDGE_H
#define HALIKEY_EDGE_H

#include <atomic>

// Same-direction dedupe helper used by HalikeyDevice to filter redundant raw events from
// either transport variant. Pure logic with no Qt dependencies so it can be unit-tested
// in isolation without linking the worker translation units.
//
// Returns true iff `raw` represents a new state versus `confirmed`, and updates `confirmed`
// to match. Returns false if `raw == confirmed` (redundant). Never discards a real transition.
//
// See docs/halikey-midi-windows-debounce-bug.md for the history that motivated removing the
// older time-window debounce from this code path.
namespace HalikeyEdge {

inline bool acceptEdge(bool raw, std::atomic<bool> &confirmed) {
    if (raw == confirmed.load(std::memory_order_acquire))
        return false;
    confirmed.store(raw, std::memory_order_release);
    return true;
}

} // namespace HalikeyEdge

#endif // HALIKEY_EDGE_H
