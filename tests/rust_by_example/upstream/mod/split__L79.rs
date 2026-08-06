// Extracted from src/mod/split.md:79
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    pub fn public_function() {
        println!("called `my::inaccessible::public_function()`");
    }
}
