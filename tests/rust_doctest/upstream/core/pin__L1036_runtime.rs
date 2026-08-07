// Extracted from library/core/src/pin.rs:1036
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    use std::future::Future;

    async fn add_one(x: u32) -> u32 {
        x + 1
    }

    fn boxed_add_one(x: u32) -> Box<dyn Future<Output = u32>> {
        Box::new(add_one(x))
    }

    let boxed_fut = boxed_add_one(42);

    // Pin the future inside the existing box
    let pinned_fut: Pin<Box<_>> = Box::into_pin(boxed_fut);
}
