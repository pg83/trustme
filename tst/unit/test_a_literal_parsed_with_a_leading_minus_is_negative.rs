// syn's `parse_negative_lit` re-parses `-` and an integer as one literal:
// `repr.parse::<Literal>().unwrap()`. Upstream's `literal_from_str` takes a
// minus right before a numeric literal into the literal's symbol, and the
// literal goes back to the compiler as `-` followed by the number.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::literal_from_text;

fn main() {
    assert_eq!(literal_from_text!("-5"), -5);
    assert_eq!(literal_from_text!("-5i64"), -5i64);
    assert_eq!(literal_from_text!("-2.5"), -2.5);
}
