// Extracted from library/core/src/str/mod.rs:1069
#![allow(unused)]
fn main() {
    let word = "goodbye";

    let count = word.char_indices().count();
    assert_eq!(7, count);

    let mut char_indices = word.char_indices();

    assert_eq!(Some((0, 'g')), char_indices.next());
    assert_eq!(Some((1, 'o')), char_indices.next());
    assert_eq!(Some((2, 'o')), char_indices.next());
    assert_eq!(Some((3, 'd')), char_indices.next());
    assert_eq!(Some((4, 'b')), char_indices.next());
    assert_eq!(Some((5, 'y')), char_indices.next());
    assert_eq!(Some((6, 'e')), char_indices.next());

    assert_eq!(None, char_indices.next());
}
