//@ aux-build: use_visibility_expansion.rs
extern crate use_visibility_expansion as source;
fn main() { assert_eq!(source::api::Token(67).0, 67); }
