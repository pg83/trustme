// Extracted from library/core/src/future/ready.rs:37
#![allow(unused)]
fn main() {
    use std::future;

    let a = future::ready(1);
    assert_eq!(a.into_inner(), 1);
}
