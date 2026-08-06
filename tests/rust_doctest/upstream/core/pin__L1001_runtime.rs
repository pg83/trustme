// Extracted from library/core/src/pin.rs:1001
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    
    // Create a value of a type that implements `Unpin`
    let mut unpin_future = std::future::ready(5);
    
    // Pin it by creating a pinning mutable reference to it (ready to be `poll`ed!)
    let my_pinned_unpin_future: Pin<&mut _> = Pin::new(&mut unpin_future);
}
