//@ aux-build: normalization_import_roots.rs
//@ compile-fail: Couldn't find node
extern crate normalization_import_roots as route;

mod route {
    pub mod operations {
        pub mod missing {
            pub struct Unique;
        }
    }
}

use route::operations::missing::Unique;

fn main() {}
