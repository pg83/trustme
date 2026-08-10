// Extracted from library/core/src/char/methods.rs:768
#![allow(unused)]
fn main() {
    assert!('a'.is_alphabetic());
    assert!('京'.is_alphabetic());

    let c = '💝';
    // love is many things, but it is not alphabetic
    assert!(!c.is_alphabetic());
}
