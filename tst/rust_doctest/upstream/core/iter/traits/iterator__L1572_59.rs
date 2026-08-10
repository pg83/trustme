// Extracted from library/core/src/iter/traits/iterator.rs:1572
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {

    let strings = "abcd".chars()
        .map_windows(|[x, y]| format!("{}+{}", x, y))
        .collect::<Vec<String>>();

    assert_eq!(strings, vec!["a+b", "b+c", "c+d"]);
}
