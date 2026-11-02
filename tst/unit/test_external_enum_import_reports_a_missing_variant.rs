//@ aux-build: coverage_path_routes.rs
//@ compile-fail: Unable to find variant

use coverage_path_routes::api::Event::Missing;
fn main() {}
