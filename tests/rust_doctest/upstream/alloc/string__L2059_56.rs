// Extracted from library/alloc/src/string.rs:2059
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("α is alpha, β is beta");
    let beta_offset = s.find('β').unwrap_or(s.len());
    
    // Replace the range up until the β from the string
    s.replace_range(..beta_offset, "Α is capital alpha; ");
    assert_eq!(s, "Α is capital alpha; β is beta");
}
