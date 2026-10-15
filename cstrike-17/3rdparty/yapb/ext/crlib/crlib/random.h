//
// crlib, simple class library for private needs.
// Copyright © RWSH Solutions LLC <lab@rwsh.ru>.
//
// SPDX-License-Identifier: MIT
//

#pragma once

#include <crlib/basic.h>
#include <crlib/traits.h>

CR_NAMESPACE_BEGIN

namespace detail {
   class SplitMix64 final {
      uint64_t state_;
   public:
      explicit SplitMix64 (uint64_t seed) noexcept : state_ (seed ^ 0x9e3779b97f4a7c15ULL) {}

   public:
      uint64_t next () noexcept {
         uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);

         z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
         z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

         return z ^ (z >> 31);
      }
   };
}

// xoshiro random number generator
class RWrand final : public Singleton <RWrand> {
   uint64_t s0_ = 0, s1_ = 0, s2_ = 0, s3_ = 0;

   [[nodiscard]] static uint64_t rotl (uint64_t x, int k) noexcept {
      return (x << k) | (x >> (64 - k));
   }

   [[nodiscard]] uint64_t next64 () noexcept {
      const uint64_t result = rotl (s1_ * 5, 7) * 9;

      const uint64_t t = s1_ << 17;

      s2_ ^= s0_;
      s3_ ^= s1_;
      s1_ ^= s2_;
      s0_ ^= s3_;

      s2_ ^= t;
      s3_ = rotl (s3_, 45);

      return result;
   }

   [[nodiscard]] uint32_t next32 () noexcept {
      return static_cast <uint32_t> (next64 ());
   }

   [[nodiscard]] uint64_t getSeed (uint64_t value = 0) {
      uint64_t seed = static_cast <uint64_t> (time (nullptr));

      seed ^= static_cast <uintptr_t> (plat.pid ());
      seed ^= static_cast <uint64_t> (clock ());
      seed ^= 0xdeadbeefULL;

      if (value > 0) {
         seed ^= value;
      }
      return seed;
   }

public:
   RWrand () noexcept { seed (); }

   void seed () noexcept {
      const uint64_t seed = getSeed ();
      detail::SplitMix64 smix (seed);

      s0_ = smix.next ();
      s1_ = smix.next ();
      s2_ = smix.next ();
      s3_ = smix.next ();
   }

   void seed (uint64_t value) noexcept {
      const uint64_t seed = getSeed (value);
      detail::SplitMix64 smix (seed);

      s0_ = smix.next ();
      s1_ = smix.next ();
      s2_ = smix.next ();
      s3_ = smix.next ();
   }

   [[nodiscard]] int32_t get (int32_t low, int32_t high) noexcept {
      if (low >= high) {
         return low;
      }
      const uint32_t range = static_cast <uint32_t> (high - low) + 1u;
      const uint64_t m = static_cast <uint64_t> (next32 ()) * range;

      return low + static_cast <int32_t> (m >> 32);
   }

   [[nodiscard]] float get (float low, float high) noexcept {
      constexpr float scale = 1.0f / 4294967296.0f;
      return low + (high - low) * (static_cast <float> (next32 ()) * scale);
   }

   [[nodiscard]] int32_t operator () (int32_t low, int32_t high) noexcept {
      return get (low, high);
   }

   [[nodiscard]] float operator () (float low, float high) noexcept {
      return get (low, high);
   }

   [[nodiscard]] bool chance (int32_t percent) noexcept {
      return get (0, 99) < percent;
   }
};

// expose global
CR_EXPOSE_GLOBAL_SINGLETON (RWrand, rg);

CR_NAMESPACE_END
