//
// crlib, simple class library for private needs.
// Copyright © RWSH Solutions LLC <lab@rwsh.ru>.
//
// SPDX-License-Identifier: MIT
//

#pragma once

#include <crlib/basic.h>
#include <crlib/array.h>
#include <crlib/string.h>
#include <crlib/twin.h>

CR_NAMESPACE_BEGIN

template <typename T> struct Hash;

template <> struct Hash <String> {
   uint32_t operator () (const String &key) const noexcept {
      return key.hash ();
   }
};

template <> struct Hash <StringRef> {
   uint32_t operator () (const StringRef &key) const noexcept {
      return key.hash ();
   }
};

template <> struct Hash <const char *> {
   uint32_t operator () (const char *key) const noexcept {
      if (!key) {
         return 0;
      }
      return StringRef::fnv1a32 (key);
   }
};

template <> struct Hash <int32_t> {
   uint32_t operator () (int32_t key) const noexcept {
      auto result = static_cast <uint32_t> (key);

      result ^= result >> 16;
      result *= 0x119de1f3;
      result ^= result >> 16;
      result *= 0x119de1f3;
      result ^= result >> 16;

      return result;
   }
};

template <typename T> struct EmptyHash {
   uint32_t operator () (T key) const noexcept {
      return static_cast <uint32_t> (key);
   }
};

namespace detail {
   enum class HashEntryStatus : uint8_t {
      Empty,
      Occupied,
      Deleted
   };

   template <typename K, typename V> struct HashEntry final : NonCopyable {
      K key {};
      V val {};

      HashEntryStatus status { HashEntryStatus::Empty };

      HashEntry () = default;
      ~HashEntry () = default;

      HashEntry (HashEntry &&rhs) noexcept
         : key (cr::move (rhs.key)), val (cr::move (rhs.val)), status (rhs.status) {
      }

      HashEntry &operator = (HashEntry &&rhs) noexcept {
         if (this != &rhs) {
            key = cr::move (rhs.key);
            val = cr::move (rhs.val);
            status = rhs.status;
         }
         return *this;
      }
   };
};

template <typename K, typename V, typename H = Hash <K>> class HashMap {
private:
   using Entry = detail::HashEntry <K, V>;
   using Status = detail::HashEntryStatus;

   H hash_ {};
   size_t length_ {};
   Array <Entry> contents_ {};

   static constexpr size_t kInitialSize = 8;
   static constexpr float kLoadFactor = 0.75f;
   static constexpr size_t kMaxSize = 1 << 24;
   static constexpr size_t kInvalidIndex = static_cast <size_t> (-1);

private:
   struct PositionResult {
      size_t index;
      bool found;
   };

   PositionResult findPosition (const K &key, uint32_t hashValue) const noexcept {
      if (contents_.empty ()) {
         return { 0, false };
      }

      size_t index = hashValue & (contents_.length () - 1);
      size_t mask = contents_.length () - 1;
      size_t steps = 0;
      size_t firstDeleted = kInvalidIndex;

      while (steps++ < contents_.length ()) {
         const auto &entry = contents_[index];

         if (entry.status == Status::Empty) {
            if (firstDeleted != kInvalidIndex) {
               return { firstDeleted, false };
            }
            return { index, false };
         }

         if (entry.status == Status::Deleted) {
            if (firstDeleted == kInvalidIndex) {
               firstDeleted = index;
            }
         }
         else if (entry.key == key) {
            return { index, true };
         }

         index = (index + 1) & mask;
      }

      if (firstDeleted != kInvalidIndex) {
         return { firstDeleted, false };
      }

      return { 0, false };
   }

   bool needsRehash () const noexcept {
      return length_ >= size_t (contents_.length () * kLoadFactor);
   }

   void rehash () noexcept {
      auto oldContents = cr::move (contents_);

      size_t newSize = oldContents.empty () ? kInitialSize :
         nextPowerOfTwo (oldContents.length () * 2);

      if (newSize > kMaxSize) {
         newSize = kMaxSize;
      }

      if (!contents_.resize (newSize)) {
         contents_ = cr::move (oldContents);
         return;
      }

      for (auto &entry : contents_) {
         entry.status = Status::Empty;
      }
      length_ = 0;

      for (auto &oldEntry : oldContents) {
         if (oldEntry.status == Status::Occupied) {
            uint32_t hashValue = hash_ (oldEntry.key);

            size_t index = hashValue & (contents_.length () - 1);
            size_t mask = contents_.length () - 1;

            while (contents_[index].status == Status::Occupied) {
               index = (index + 1) & mask;
            }

            contents_[index].key = cr::move (oldEntry.key);
            contents_[index].val = cr::move (oldEntry.val);
            contents_[index].status = Status::Occupied;

            ++length_;
         }
      }
   }

   static size_t nextPowerOfTwo (size_t n) noexcept {
      if (n <= 2) {
         return 2;
      }
      n--;

      n |= n >> 1;
      n |= n >> 2;
      n |= n >> 4;
      n |= n >> 8;
      n |= n >> 16;

      if constexpr (sizeof (size_t) > 4) {
         n |= n >> 32;
      }
      return n + 1;
   }

public:
   HashMap () {
      contents_.resize (kInitialSize);
   }

   HashMap (HashMap &&rhs) noexcept
      : contents_ (cr::move (rhs.contents_)), length_ (rhs.length_), hash_ (cr::move (rhs.hash_)) {
   }

   HashMap (std::initializer_list <Twin <K, V>> list) {
      const size_t cap = nextPowerOfTwo (list.size ());
      contents_.resize (cap > kInitialSize ? cap : kInitialSize);

      for (const auto &elem : list) {
         operator[] (elem.first) = cr::move (elem.second);
      }
   }

   ~HashMap () = default;

   HashMap &operator = (HashMap &&rhs) noexcept {
      if (this != &rhs) {
         contents_ = cr::move (rhs.contents_);
         length_ = rhs.length_;
         hash_ = cr::move (rhs.hash_);
      }
      return *this;
   }

public:
   template <bool IsConst> class HashMapIterator {
   private:
      using EntryType = typename cr::conditional<IsConst, const Entry, Entry>::type;

      EntryType *current_ {};
      EntryType *end_ {};

      void advanceToNextOccupied () noexcept {
         while (current_ != end_ && current_->status != Status::Occupied) {
            ++current_;
         }
      }

   public:
      using ValueType = typename cr::conditional <IsConst,
         cr::Twin <const K &, const V &>, cr::Twin <const K &, V &>>::type;

      using Reference = ValueType;
      using Pointer = void;

      HashMapIterator (EntryType *current, EntryType *end)
         : current_ (current), end_ (end) {

         advanceToNextOccupied ();
      }

      Reference operator * () const noexcept {
         return Reference (current_->key, current_->val);
      }

      HashMapIterator &operator ++ () noexcept {
         ++current_;
         advanceToNextOccupied ();

         return *this;
      }

      HashMapIterator operator ++ (int) noexcept {
         HashMapIterator tmp = *this;
         ++(*this);

         return tmp;
      }

      bool operator == (const HashMapIterator &other) const noexcept {
         return current_ == other.current_;
      }

      bool operator != (const HashMapIterator &other) const noexcept {
         return !(*this == other);
      }
   };

   using iterator = HashMapIterator<false>;
   using const_iterator = HashMapIterator<true>;

   iterator begin () noexcept {
      return iterator (contents_.data (), contents_.data () + contents_.length ());
   }

   iterator end () noexcept {
      return iterator (contents_.data () + contents_.length (), contents_.data () + contents_.length ());
   }

   const_iterator begin () const noexcept {
      return cbegin ();
   }

   const_iterator end () const noexcept {
      return cend ();
   }

   const_iterator cbegin () const noexcept {
      return const_iterator (contents_.data (), contents_.data () + contents_.length ());
   }

   const_iterator cend () const noexcept {
      return const_iterator (contents_.data () + contents_.length (), contents_.data () + contents_.length ());
   }

public:
   V &operator [] (const K &key) noexcept {
      if (contents_.empty ()) {
         rehash ();

         if (contents_.empty ()) {
            contents_.resize (kInitialSize);
         }
      }

      uint32_t hashValue = hash_ (key);
      auto result = findPosition (key, hashValue);

      if (!result.found) {
         if (needsRehash ()) {
            rehash ();
            result = findPosition (key, hashValue);
         }
         contents_[result.index].key = key;
         contents_[result.index].status = Status::Occupied;
         ++length_;
      }
      return contents_[result.index].val;
   }

   bool insert (const K &key, const V &val) noexcept {
      if (contents_.empty ()) {
         rehash ();

         if (contents_.empty ()) {
            return false;
         }
      }

      uint32_t hashValue = hash_ (key);
      auto result = findPosition (key, hashValue);

      if (result.found) {
         return false;
      }

      if (needsRehash ()) {
         rehash ();
         result = findPosition (key, hashValue);

         if (result.found) {
            return false;
         }
      }

      contents_[result.index].key = key;
      contents_[result.index].val = val;
      contents_[result.index].status = Status::Occupied;
      ++length_;

      return true;
   }

   bool insert (const K &key, V &&val) noexcept {
      if (contents_.empty ()) {
         rehash ();

         if (contents_.empty ()) {
            return false;
         }
      }

      uint32_t hashValue = hash_ (key);
      auto result = findPosition (key, hashValue);

      if (result.found) {
         return false;
      }

      if (needsRehash ()) {
         rehash ();
         result = findPosition (key, hashValue);

         if (result.found) {
            return false;
         }
      }

      contents_[result.index].key = key;
      contents_[result.index].val = cr::move (val);
      contents_[result.index].status = Status::Occupied;
      ++length_;

      return true;
   }

   size_t erase (const K &key) noexcept {
      if (contents_.empty ()) {
         return 0;
      }

      uint32_t hashValue = hash_ (key);
      auto result = findPosition (key, hashValue);

      if (!result.found) {
         return 0;
      }

      contents_[result.index].status = Status::Deleted;
      --length_;

      return 1;
   }

   bool exists (const K &key) const noexcept {
      if (contents_.empty ()) {
         return false;
      }

      uint32_t hashValue = hash_ (key);
      auto result = findPosition (key, hashValue);

      return result.found;
   }

   void clear () noexcept {
      length_ = 0;

      for (auto &entry : contents_) {
         entry.status = Status::Empty;
      }
   }

   void zap () noexcept {
      clear ();

      contents_.clear ();
      contents_.resize (kInitialSize);
      contents_.shrink ();
   }

   constexpr size_t length () const noexcept {
      return length_;
   }

   constexpr bool empty () const noexcept {
      return length_ == 0;
   }

   size_t capacity () const noexcept {
      return contents_.length ();
   }

   void reserve (size_t n) noexcept {
      n = nextPowerOfTwo (n);

      if (n > contents_.length ()) {
         rehash ();
      }
   }
};

CR_NAMESPACE_END
