// Extracted from library/std/src/collections/hash/set.rs:552
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([4, 2, 3, 4]);

    // Can be seen as `a - b`.
    for x in a.difference(&b) {
        println!("{x}"); // Print 1
    }

    let diff: HashSet<_> = a.difference(&b).collect();
    assert_eq!(diff, [1].iter().collect());

    // Note that difference is not symmetric,
    // and `b - a` means something else:
    let diff: HashSet<_> = b.difference(&a).collect();
    assert_eq!(diff, [4].iter().collect());
}
