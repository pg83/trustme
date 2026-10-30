//@ aux-build: trait_glob_value_source.rs
#![feature(import_trait_associated_functions)]
use trait_glob_value_source::SourceAlias::Item;
fn main() {}
