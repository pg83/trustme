//@ compile-variant: -O

use std::ops::RangeBounds;

fn main() {
    assert!((1..4).contains(&3));
    assert!(!(1..4).contains(&4));
    assert!((1..=4).contains(&4));
    assert!((..4).contains(&3));
    assert!((..=4).contains(&4));
    assert!((1..).contains(&7));
    assert!((..).contains(&-7));
}
