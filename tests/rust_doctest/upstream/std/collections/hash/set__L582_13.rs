// Extracted from library/std/src/collections/hash/set.rs:582
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([4, 2, 3, 4]);

    // Print 1, 4 in arbitrary order.
    for x in a.symmetric_difference(&b) {
        println!("{x}");
    }

    let diff1: HashSet<_> = a.symmetric_difference(&b).collect();
    let diff2: HashSet<_> = b.symmetric_difference(&a).collect();

    assert_eq!(diff1, diff2);
    assert_eq!(diff1, [1, 4].iter().collect());
}
