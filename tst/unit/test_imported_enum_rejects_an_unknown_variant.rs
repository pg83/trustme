//@ aux-build: reexported_use_targets.rs
//@ compile-fail: Unknown enum variant
mod facade { pub use reexported_use_targets::Mode; }
use crate::facade::Mode::Missing;
fn main() {}
