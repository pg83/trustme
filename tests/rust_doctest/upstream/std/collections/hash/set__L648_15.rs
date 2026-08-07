// Extracted from library/std/src/collections/hash/set.rs:648
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([4, 2, 3, 4]);

    // Print 1, 2, 3, 4 in arbitrary order.
    for x in a.union(&b) {
        println!("{x}");
    }

    let union: HashSet<_> = a.union(&b).collect();
    assert_eq!(union, [1, 2, 3, 4].iter().collect());
}
