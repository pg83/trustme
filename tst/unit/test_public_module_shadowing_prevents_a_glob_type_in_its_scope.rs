//@ compile-fail: error
#![allow(dead_code, unused_imports, non_snake_case)]
mod origin { pub struct Shadow; }
mod bridge {
    pub mod Shadow {}
    pub use crate::origin::*;
    pub fn check() { let _ = std::mem::size_of::<Shadow>(); }
}
fn main() { bridge::check(); }
