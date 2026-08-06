// Extracted from library/std/src/collections/hash/set.rs:619
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([4, 2, 3, 4]);
    
    // Print 2, 3 in arbitrary order.
    for x in a.intersection(&b) {
        println!("{x}");
    }
    
    let intersection: HashSet<_> = a.intersection(&b).collect();
    assert_eq!(intersection, [2, 3].iter().collect());
}
