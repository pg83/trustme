// Extracted from library/core/src/iter/traits/iterator.rs:1514
#![allow(unused)]
fn main() {
    let words = ["alpha", "beta", "gamma"];
    
    // chars() returns an iterator
    let merged: String = words.iter()
                              .flat_map(|s| s.chars())
                              .collect();
    assert_eq!(merged, "alphabetagamma");
}
