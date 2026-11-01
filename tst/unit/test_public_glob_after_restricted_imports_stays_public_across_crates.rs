//@ aux-build: restricted_glob_exports.rs
extern crate restricted_glob_exports as source;
fn main() { assert_eq!(source::container::api::Token(31).0, 31); }
