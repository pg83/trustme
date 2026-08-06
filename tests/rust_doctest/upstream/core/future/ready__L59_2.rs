// Extracted from library/core/src/future/ready.rs:59
#![allow(unused)]
fn main() {
    use std::future;
    
    async fn run() {
    let a = future::ready(1);
    assert_eq!(a.await, 1);
    }
}
