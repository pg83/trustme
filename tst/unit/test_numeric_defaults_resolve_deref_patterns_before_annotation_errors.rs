//@ compile-flags: -O
//@ compile-fail: error:
#![feature(deref_patterns, deref_pure_trait)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefPure};

struct Numeric<T>(T, u8);
impl Deref for Numeric<i32> { type Target = u8; fn deref(&self) -> &u8 { &self.1 } }
impl Deref for Numeric<i64> { type Target = u8; fn deref(&self) -> &u8 { &self.1 } }
impl Deref for Numeric<f32> { type Target = u8; fn deref(&self) -> &u8 { &self.1 } }
impl Deref for Numeric<f64> { type Target = u8; fn deref(&self) -> &u8 { &self.1 } }
unsafe impl DerefPure for Numeric<i32> {}
unsafe impl DerefPure for Numeric<i64> {}
unsafe impl DerefPure for Numeric<f32> {}
unsafe impl DerefPure for Numeric<f64> {}

fn main() {
    assert!(matches!(Numeric(0, 17), deref!(17)));
    assert!(matches!(Numeric(0.0, 23), deref!(23)));
}
