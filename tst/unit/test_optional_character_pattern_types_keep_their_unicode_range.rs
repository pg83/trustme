#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Letter = pattern_type!(char is 'm'..='q');
fn main() {
    let letter: Letter = unsafe { std::mem::transmute('n') };
    let present: Option<Letter> = Some(letter);
    assert_eq!(std::mem::size_of::<Option<Letter>>(), std::mem::size_of::<char>());
    assert_eq!(unsafe { std::mem::transmute::<Letter, char>(present.unwrap()) }, 'n');
    assert!(Option::<Letter>::None.is_none());
}
