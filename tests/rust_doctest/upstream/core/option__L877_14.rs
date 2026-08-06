// Extracted from library/core/src/option.rs:877
#![allow(unused)]
fn main() {
    let mut x = Some(1234);
    x.as_mut_slice()[0] += 1;
    assert_eq!(x, Some(1235));
}
