//@ compile-flags: -Cemit-cpp-only
//@ crate-type: lib
#[target_feature(enable = "rdrand")]
pub fn rdrand_feature() {}

#[target_feature(enable = "bmi1")]
pub fn bmi_feature() {}

#[target_feature(enable = "cmpxchg16b")]
pub fn cmpxchg_feature() {}

#[target_feature(enable = "lahfsahf")]
pub fn lahf_feature() {}

#[target_feature(enable = "avx10.1")]
pub fn avx10_1_feature() {}

#[target_feature(enable = "avx10.2")]
pub fn avx10_2_feature() {}

#[target_feature(enable = "apxf")]
pub fn apxf_feature() {}

#[target_feature(enable = "ermsb")]
pub fn ermsb_feature() {}

#[target_feature(enable = "soft-float")]
pub fn soft_float_feature() {}
