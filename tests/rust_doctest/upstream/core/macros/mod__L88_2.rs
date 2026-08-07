// Extracted from library/core/src/macros/mod.rs:88
#![allow(unused)]
fn main() {
    let a = 3;
    let b = 2;
    assert_ne!(a, b);

    assert_ne!(a, b, "we are testing that the values are not equal");
}
