// Extracted from library/std/src/panicking.rs:133
#![allow(unused)]
fn main() {
    use std::panic;
    
    panic::set_hook(Box::new(|_| {
        println!("Custom panic hook");
    }));
    
    panic!("Normal panic");
}
