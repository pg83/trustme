// Extracted from library/core/src/option.rs:2025
#![allow(unused)]
fn main() {
    let x = 12;
    let opt_x = Some(&x);
    assert_eq!(opt_x, Some(&12));
    let cloned = opt_x.cloned();
    assert_eq!(cloned, Some(12));
}
