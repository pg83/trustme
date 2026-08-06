// Extracted from library/core/src/option.rs:1108
#![allow(unused)]
fn main() {
    let x = Some("air");
    assert_eq!(unsafe { x.unwrap_unchecked() }, "air");
}
