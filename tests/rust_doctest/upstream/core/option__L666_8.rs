// Extracted from library/core/src/option.rs:666
#![allow(unused)]
fn main() {
    let x: Option<u32> = Some(2);
    assert_eq!(x.is_none(), false);

    let x: Option<u32> = None;
    assert_eq!(x.is_none(), true);
}
