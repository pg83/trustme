// Extracted from library/core/src/option.rs:2671
#![allow(unused)]
fn main() {
    let x: Option<Option<Option<u32>>> = Some(Some(Some(6)));
    assert_eq!(Some(Some(6)), x.flatten());
    assert_eq!(Some(6), x.flatten().flatten());
}
