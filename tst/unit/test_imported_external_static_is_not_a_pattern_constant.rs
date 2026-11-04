//@ aux-build: mutable_definition_site.rs
//@ compile-fail: error

use mutable_definition_site::COUNTER;

fn main() {
    match 223u32 {
        COUNTER => (),
    }
}
