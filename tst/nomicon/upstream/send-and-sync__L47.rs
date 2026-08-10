// Extracted from src/send-and-sync.md:47
#![allow(unused)]
fn main() {
    struct MyBox(*mut u8);
    
    unsafe impl Send for MyBox {}
    unsafe impl Sync for MyBox {}
}
