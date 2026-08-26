//@ compile-flags: -Znext-solver=globally

// A literal (integer/float class) inference variable can only become one of
// its class's primitives: no primitive implements Iterator, so
// `(&mut ?int).partial_cmp(..)` must not select Iterator::partial_cmp
// through the `&mut I` blanket over PartialOrd.  Mirrors coretests
// cmp.rs::test_mut_int_totalord.

use std::cmp::Ordering;

fn main() {
    let mut a = 5;
    let mut b = 3;
    assert_eq!((&mut a).cmp(&&mut b), Ordering::Greater);
    assert_eq!((&mut a).partial_cmp(&&mut b), Some(Ordering::Greater));
    let mut x = 1.5f64;
    let y = &mut x;
    assert_eq!(y.total_cmp(&1.5f64), Ordering::Equal);
}
