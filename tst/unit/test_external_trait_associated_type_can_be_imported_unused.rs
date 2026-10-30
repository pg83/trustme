//@ aux-build: trait_glob_value_source.rs
#![feature(import_trait_associated_functions)]
use trait_glob_value_source::Source::Item as Direct;
mod bridge { pub use trait_glob_value_source::Source::*; }
use bridge::Item;
fn main() {}
