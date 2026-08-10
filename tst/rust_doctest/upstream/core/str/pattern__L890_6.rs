// Extracted from library/core/src/str/pattern.rs:890
#![allow(unused)]
fn main() {
    assert_eq!("Hello world".find(&['o', 'l'][..]), Some(2));
    assert_eq!("Hello world".find(&['h', 'w'][..]), Some(6));
}
