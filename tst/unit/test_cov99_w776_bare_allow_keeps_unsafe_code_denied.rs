//@ compile-fail: usage of an `unsafe` block
#![deny(unsafe_code)]

#[allow]
fn main() {
    unsafe {}
}
