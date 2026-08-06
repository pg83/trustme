// Extracted from library/core/src/num/error.rs:65
#![allow(unused)]
fn main() {
    if let Err(e) = i32::from_str_radix("a12", 10) {
        println!("Failed conversion to i32: {e}");
    }
}
