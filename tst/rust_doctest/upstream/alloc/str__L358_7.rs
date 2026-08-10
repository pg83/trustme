// Extracted from library/alloc/src/str.rs:358
#![allow(unused)]
extern crate alloc;
fn main() {
    let sigma = "Σ";

    assert_eq!("σ", sigma.to_lowercase());

    // but at the end of a word, it's ς, not σ:
    let odysseus = "ὈΔΥΣΣΕΎΣ";

    assert_eq!("ὀδυσσεύς", odysseus.to_lowercase());
}
