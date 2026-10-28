//@ compile-flags: -Ctarget-feature=+rdrand,+bmi1,+cmpxchg16b,+lahfsahf,+avx10.1,+avx10.2,+apxf,+ermsb,+soft-float -Cemit-cpp-only
//@ crate-type: lib
pub fn feature_mapping_is_emitted() {}
