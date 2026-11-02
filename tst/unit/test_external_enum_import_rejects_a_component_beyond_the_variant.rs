//@ aux-build: coverage_path_routes.rs
//@ compile-fail: Encountered enum at unexpected location in import

use coverage_path_routes::api::Event::Data::Extra;
fn main() {}
