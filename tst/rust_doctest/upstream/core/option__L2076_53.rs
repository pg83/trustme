// Extracted from library/core/src/option.rs:2076
#![allow(unused)]
fn main() {
    let mut x = 12;
    let opt_x = Some(&mut x);
    assert_eq!(opt_x, Some(&mut 12));
    let cloned = opt_x.cloned();
    assert_eq!(cloned, Some(12));
}
