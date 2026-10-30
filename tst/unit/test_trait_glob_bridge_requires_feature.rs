//@ compile-fail: import_trait_associated_functions
mod definitions { pub trait Source { fn value() -> u16; } }
mod bridge { pub use super::definitions::Source::*; }
use bridge::value;
fn main() {}
