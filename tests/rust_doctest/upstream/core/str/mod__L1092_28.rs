// Extracted from library/core/src/str/mod.rs:1092
#![allow(unused)]
fn main() {
    let yes = "y̆es";

    let mut char_indices = yes.char_indices();

    assert_eq!(Some((0, 'y')), char_indices.next()); // not (0, 'y̆')
    assert_eq!(Some((1, '\u{0306}')), char_indices.next());

    // note the 3 here - the previous character took up two bytes
    assert_eq!(Some((3, 'e')), char_indices.next());
    assert_eq!(Some((4, 's')), char_indices.next());

    assert_eq!(None, char_indices.next());
}
