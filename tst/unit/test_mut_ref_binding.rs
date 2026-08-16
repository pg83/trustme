// `let mut ref x = ...` binds a reference through a mutable slot, so both
// markers may appear, in either order. Only `ref mut` was parsed.
//
// Same shape as the upstream test mut/mut-ref.rs.
#![allow(incomplete_features, unused)]
#![feature(mut_ref)]

fn main() {
    let mut ref x = 10;
    assert_eq!(*x, 10);
    x = &11;
    assert_eq!(*x, 11);

    let ref mut y = 12;
    *y = 13;
    assert_eq!(*y, 13);

    let mut ref mut z = 14;
    assert_eq!(*z, 14);
    let mut other = 15;
    z = &mut other;
    assert_eq!(*z, 15);
}
