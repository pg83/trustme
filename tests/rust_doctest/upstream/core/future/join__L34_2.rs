// Extracted from library/core/src/future/join.rs:34
#![allow(unused)]
#![feature(future_join)]
fn main() {
    
    use std::future::join;
    
    async fn one() -> usize { 1 }
    async fn two() -> usize { 2 }
    async fn three() -> usize { 3 }
    
    let _ = async {
    let x = join!(one(), two(), three()).await;
    assert_eq!(x, (1, 2, 3));
    };
}
