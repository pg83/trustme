//@ compile-fail: Encountered trait at unexpected location in import
#![feature(import_trait_associated_functions)]
trait Source { fn read(); }
use crate::Source::read::Missing;
fn main() {}
