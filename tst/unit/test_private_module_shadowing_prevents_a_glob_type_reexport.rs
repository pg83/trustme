//@ compile-fail: error
#![allow(dead_code, unused_imports, non_snake_case)]
mod origin {
    pub struct Shadow;
    impl Shadow { pub fn answer() -> u32 { 37 } }
}
mod bridge {
    mod Shadow {}
    pub use crate::origin::*;
}
use bridge::*;
fn main() { let _ = Shadow::answer(); }
