// Extracted from library/core/src/pin.rs:1019
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    
    async fn add_one(x: u32) -> u32 {
        x + 1
    }
    
    // Call the async function to get a future back
    let fut = add_one(42);
    
    // Pin the future inside a pinning box
    let pinned_fut: Pin<Box<_>> = Box::pin(fut);
}
