//@ compile-fail: import_trait_associated_functions
mod definitions {
    pub trait Source { const WIDTH: usize; type Item; fn value() -> u16; }
}
use definitions::Source::*;
fn main() {}
