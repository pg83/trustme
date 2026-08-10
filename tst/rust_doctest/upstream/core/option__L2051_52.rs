// Extracted from library/core/src/option.rs:2051
#![allow(unused)]
fn main() {
    let mut x = 12;
    let opt_x = Some(&mut x);
    assert_eq!(opt_x, Some(&mut 12));
    let copied = opt_x.copied();
    assert_eq!(copied, Some(12));
}
