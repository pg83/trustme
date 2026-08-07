// Extracted from library/core/src/char/methods.rs:879
#![allow(unused)]
fn main() {
    assert!(' '.is_whitespace());

    // line break
    assert!('\n'.is_whitespace());

    // a non-breaking space
    assert!('\u{A0}'.is_whitespace());

    assert!(!'越'.is_whitespace());
}
