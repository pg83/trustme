// Extracted from library/core/src/macros/mod.rs:679
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    fn foo(x: Option<i32>) {
        match x {
            Some(n) if n >= 0 => println!("Some(Non-negative)"),
            Some(n) if n <  0 => println!("Some(Negative)"),
            Some(_)           => unreachable!(), // compile error if commented out
            None              => println!("None")
        }
    }
}
