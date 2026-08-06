// Extracted from library/core/src/mem/drop_guard.rs:50
#![allow(unused)]
#![allow(unused)]
#![feature(drop_guard)]
fn main() {
    
    use std::mem::DropGuard;
    
    let value = String::from("Chashu likes tuna");
    let guard = DropGuard::new(value, |s| println!("{s}"));
}
