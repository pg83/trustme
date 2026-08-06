// Extracted from library/core/src/pin.rs:1459
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    
    let mut val: u8 = 5;
    let mut pinned: Pin<&mut u8> = Pin::new(&mut val);
    println!("{}", pinned); // 5
    pinned.set(10);
    println!("{}", pinned); // 10
}
