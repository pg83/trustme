//@ aux-build: coverage_path_routes.rs
//@ compile-fail: Unable to find associated item
#![feature(import_trait_associated_functions)]

use coverage_path_routes::api::Schema::Missing;
fn main() {}
