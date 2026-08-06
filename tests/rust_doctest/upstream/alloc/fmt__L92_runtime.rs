// Extracted from library/alloc/src/fmt.rs:92
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    let argument = 2 + 2;
    format!("{argument}");   // => "4"
    
    fn make_string(a: u32, b: &str) -> String {
        format!("{b} {a}")
    }
    make_string(927, "label"); // => "label 927"
}
