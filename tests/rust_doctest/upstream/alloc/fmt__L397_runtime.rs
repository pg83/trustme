// Extracted from library/alloc/src/fmt.rs:397
#![allow(unused)]
#![allow(dead_code)]
extern crate alloc;
fn main() {
    use std::fmt;
    struct Foo; // our custom type
    impl fmt::Display for Foo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    write!(f, "testing, testing")
    } }
}
