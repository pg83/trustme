// Extracted from library/alloc/src/string.rs:1955
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("α is alpha, β is beta");
    let beta_offset = s.find('β').unwrap_or(s.len());

    // Remove the range up until the β from the string
    let t: String = s.drain(..beta_offset).collect();
    assert_eq!(t, "α is alpha, ");
    assert_eq!(s, "β is beta");

    // A full range clears the string, like `clear()` does
    s.drain(..);
    assert_eq!(s, "");
}
