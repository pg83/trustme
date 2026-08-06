// Extracted from library/alloc/src/str.rs:451
#![allow(unused)]
extern crate alloc;
fn main() {
    let new_year = "农历新年";
    
    assert_eq!(new_year, new_year.to_uppercase());
}
