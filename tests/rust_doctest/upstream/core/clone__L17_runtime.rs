// Extracted from library/core/src/clone.rs:17
#![allow(unused)]
fn main() {
    let s = String::new(); // String type implements Clone
    let copy = s.clone(); // so we can clone it
}
