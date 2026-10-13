// oPlaits - monophonic note stack (last-note priority). MIT.
//
// A utility TYPE, not shared state: each engine owns its own instance, so
// the notes one engine believes are held never leak into the other.
#ifndef OPLAITS_NOTE_STACK_H_
#define OPLAITS_NOTE_STACK_H_

#include <stdint.h>

namespace oplaits {

class NoteStack {
 public:
  static const int kCapacity = 16;

  void Clear() { size_ = 0; }
  bool empty() const { return size_ == 0; }
  int size() const { return size_; }
  // The most recently pressed note still held. Only valid when !empty().
  uint8_t top() const { return notes_[size_ - 1]; }
  bool Contains(uint8_t note) const {
    for (int i = 0; i < size_; ++i) if (notes_[i] == note) return true;
    return false;
  }

  // A repeated note moves to the top. When full, the OLDEST note is dropped,
  // which can only strand a note that is no longer sounding (mono voice).
  void Push(uint8_t note) {
    Remove(note);
    if (size_ == kCapacity) {
      for (int i = 1; i < size_; ++i) notes_[i - 1] = notes_[i];
      --size_;
    }
    notes_[size_++] = note;
  }

  // Returns true if the note was held.
  bool Remove(uint8_t note) {
    for (int i = 0; i < size_; ++i) {
      if (notes_[i] == note) {
        for (int j = i + 1; j < size_; ++j) notes_[j - 1] = notes_[j];
        --size_;
        return true;
      }
    }
    return false;
  }

 private:
  uint8_t notes_[kCapacity];
  int size_;
};

}  // namespace oplaits

#endif  // OPLAITS_NOTE_STACK_H_
