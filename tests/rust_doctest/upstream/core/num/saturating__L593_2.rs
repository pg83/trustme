// Extracted from library/core/src/num/saturating.rs:593
#![allow(unused)]
fn main() {
    use std::num::Saturating;

    let n: Saturating<i64> = Saturating(0x0123456789ABCDEF);
    let m: Saturating<i64> = Saturating(-0x76543210FEDCBA99);

    assert_eq!(n.rotate_left(32), m);
}
