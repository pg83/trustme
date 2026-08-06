// Extracted from library/core/src/ops/range.rs:654
#![allow(unused)]
fn main() {
    use std::collections::BTreeMap;
    use std::ops::Bound::{Excluded, Included, Unbounded};
    
    let mut map = BTreeMap::new();
    map.insert(3, "a");
    map.insert(5, "b");
    map.insert(8, "c");
    
    for (key, value) in map.range((Excluded(3), Included(8))) {
        println!("{key}: {value}");
    }
    
    assert_eq!(Some((&3, &"a")), map.range((Unbounded, Included(5))).next());
}
