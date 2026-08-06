// Extracted from library/alloc/src/vec/mod.rs:3277
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut characters = vec!['a', 'b', 'c', 'd', 'e'];
    characters.extend_from_within(2..);
    assert_eq!(characters, ['a', 'b', 'c', 'd', 'e', 'c', 'd', 'e']);
    
    let mut numbers = vec![0, 1, 2, 3, 4];
    numbers.extend_from_within(..2);
    assert_eq!(numbers, [0, 1, 2, 3, 4, 0, 1]);
    
    let mut strings = vec![String::from("hello"), String::from("world"), String::from("!")];
    strings.extend_from_within(1..=2);
    assert_eq!(strings, ["hello", "world", "!", "world", "!"]);
}
