// Extracted from library/core/src/option.rs:1469
#![allow(unused)]
fn main() {
    let x = Some(2);
    let y: Option<&str> = None;
    assert_eq!(x.and(y), None);

    let x: Option<u32> = None;
    let y = Some("foo");
    assert_eq!(x.and(y), None);

    let x = Some(2);
    let y = Some("foo");
    assert_eq!(x.and(y), Some("foo"));

    let x: Option<u32> = None;
    let y: Option<&str> = None;
    assert_eq!(x.and(y), None);
}
