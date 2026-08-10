// Extracted from library/core/src/option.rs:1903
#![allow(unused)]
fn main() {
    let x = Some(1);
    let y = Some("hi");
    let z = None::<u8>;

    assert_eq!(x.zip(y), Some((1, "hi")));
    assert_eq!(x.zip(z), None);
}
