// Extracted from library/core/src/num/dec2flt/mod.rs:201
#![allow(unused)]
fn main() {
    use std::str::FromStr;
    
    if let Err(e) = f64::from_str("a.12") {
        println!("Failed conversion to f64: {e}");
    }
}
