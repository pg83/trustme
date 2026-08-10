// Extracted from library/core/src/option.rs:2243
#![allow(unused)]
fn main() {
    let o: Option<u8> = Option::from(67);

    assert_eq!(Some(67), o);
}
