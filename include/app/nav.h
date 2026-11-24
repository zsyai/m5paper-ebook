#pragma once

// What a page wants to happen after handling an event, and the stack of
// pages the app keeps to make "back" mean "where I came from".
//
//   Stay           nothing
//   Push(s)        open s on top; Back returns here
//   Replace(s)     swap this page for s; Back from s skips this page
//   Back           close this page, return to the one below
//   BackTo(s)      close pages until s is on top (no-op if s is not below)
//   Root(s)        forget the stack and start over at s (bottom-bar
//                  navigation between 书架 / 传书 / 设置 / 阅读)

#include <cstdint>

#include "app/state.h"

namespace reader {
namespace app {

struct Nav {
  enum class Kind : uint8_t { kStay, kPush, kReplace, kBack, kBackTo, kRoot };

  Kind kind = Kind::kStay;
  State state = State::BOOKSHELF;

  static Nav Stay() { return Nav{}; }
  static Nav Push(State s) { return Nav{Kind::kPush, s}; }
  static Nav Replace(State s) { return Nav{Kind::kReplace, s}; }
  static Nav Back() { return Nav{Kind::kBack, State::BOOKSHELF}; }
  static Nav BackTo(State s) { return Nav{Kind::kBackTo, s}; }
  static Nav Root(State s) { return Nav{Kind::kRoot, s}; }

 private:
  Nav() = default;
  Nav(Kind k, State s) : kind(k), state(s) {}
};

// Fixed-capacity stack of States; the bottom entry is always present.
class NavStack {
 public:
  static constexpr int kCapacity = 8;

  explicit NavStack(State root = State::BOOKSHELF) { Reset(root); }

  void Reset(State root) {
    size_ = 1;
    states_[0] = root;
  }
  State top() const { return states_[size_ - 1]; }
  int size() const { return size_; }
  State at(int i) const { return states_[i]; }
  bool Contains(State s) const {
    for (int i = 0; i < size_; ++i) {
      if (states_[i] == s) return true;
    }
    return false;
  }

  // Applies `nav`; returns true if top() changed. A Push on a full stack
  // behaves like Replace so the current page is never lost.
  bool Apply(const Nav& nav) {
    const State before = top();
    switch (nav.kind) {
      case Nav::Kind::kStay:
        break;
      case Nav::Kind::kPush:
        if (size_ < kCapacity) ++size_;
        states_[size_ - 1] = nav.state;
        break;
      case Nav::Kind::kReplace:
        states_[size_ - 1] = nav.state;
        break;
      case Nav::Kind::kBack:
        if (size_ > 1) --size_;
        break;
      case Nav::Kind::kBackTo:
        if (Contains(nav.state)) {
          while (top() != nav.state) --size_;
        }
        break;
      case Nav::Kind::kRoot:
        Reset(nav.state);
        break;
    }
    return top() != before;
  }

 private:
  State states_[kCapacity];
  int size_ = 1;
};

}  // namespace app
}  // namespace reader
