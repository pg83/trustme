#![feature(import_trait_associated_functions)]
trait Source { type Item; }
use Source::Item as Direct;
mod bridge { pub use super::Source::*; }
use bridge::Item;
fn main() {}
