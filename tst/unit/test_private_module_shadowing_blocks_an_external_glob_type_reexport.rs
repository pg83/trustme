//@ compile-fail: error
#![allow(dead_code, unused_imports, non_snake_case)]
mod bridge {
    mod NonZeroU8 {}
    pub use std::num::*;
}
use bridge::*;
fn main() { let _ = NonZeroU8::new(7); }
