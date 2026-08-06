// Extracted from library/core/src/iter/traits/iterator.rs:2318
#![allow(unused)]
#![feature(iter_is_partitioned)]
fn main() {
    
    assert!("Iterator".chars().is_partitioned(char::is_uppercase));
    assert!(!"IntoIterator".chars().is_partitioned(char::is_uppercase));
}
