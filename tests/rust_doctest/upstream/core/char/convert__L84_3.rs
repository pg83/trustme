// Extracted from library/core/src/char/convert.rs:84
#![allow(unused)]
fn main() {
    let c = '⚙';
    let u = u128::from(c);
    assert!(16 == size_of_val(&u))
}
