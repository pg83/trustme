// Extracted from library/core/src/option.rs:1998
#![allow(unused)]
fn main() {
    let x = 12;
    let opt_x = Some(&x);
    assert_eq!(opt_x, Some(&12));
    let copied = opt_x.copied();
    assert_eq!(copied, Some(12));
}
