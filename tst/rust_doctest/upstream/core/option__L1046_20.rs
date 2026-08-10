// Extracted from library/core/src/option.rs:1046
#![allow(unused)]
fn main() {
    let k = 10;
    assert_eq!(Some(4).unwrap_or_else(|| 2 * k), 4);
    assert_eq!(None.unwrap_or_else(|| 2 * k), 20);
}
