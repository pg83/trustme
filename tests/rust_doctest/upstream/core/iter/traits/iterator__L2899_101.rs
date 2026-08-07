// Extracted from library/core/src/iter/traits/iterator.rs:2899
#![allow(unused)]
fn main() {
    let a = ["lol", "NaN", "2", "5"];

    let first_number = a.iter().find_map(|s| s.parse().ok());

    assert_eq!(first_number, Some(2));
}
