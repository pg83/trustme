// Extracted from library/core/src/iter/traits/iterator.rs:1500
#![allow(unused)]
fn main() {
    let words = ["alpha", "beta", "gamma"];
    
    // chars() returns an iterator
    let merged: String = words.iter()
                              .map(|s| s.chars())
                              .flatten()
                              .collect();
    assert_eq!(merged, "alphabetagamma");
}
