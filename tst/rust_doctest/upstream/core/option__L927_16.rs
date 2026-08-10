// Extracted from library/core/src/option.rs:927
#![allow(unused)]
fn main() {
    let x = Some("value");
    assert_eq!(x.expect("fruits are healthy"), "value");
}
