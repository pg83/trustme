// Extracted from library/core/src/iter/traits/accum.rs:228
#![allow(unused)]
fn main() {
    let words = vec!["have", "a", "great", "day"];
    let total: Option<usize> = words.iter().map(|w| w.find('a')).sum();
    assert_eq!(total, Some(5));
    let words = vec!["have", "a", "good", "day"];
    let total: Option<usize> = words.iter().map(|w| w.find('a')).sum();
    assert_eq!(total, None);
}
