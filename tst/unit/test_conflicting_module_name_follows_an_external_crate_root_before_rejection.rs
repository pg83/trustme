//@ aux-build: normalization_import_roots.rs
//@ compile-fail: Couldn't find final node
extern crate normalization_import_roots as route;

mod route {
    pub mod root {
        pub struct Unique;
    }
}

use route::root::Unique;

fn main() {}
