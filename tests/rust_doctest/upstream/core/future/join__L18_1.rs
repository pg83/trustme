// Extracted from library/core/src/future/join.rs:18
#![allow(unused)]
#![feature(future_join)]
fn main() {
    
    use std::future::join;
    
    async fn one() -> usize { 1 }
    async fn two() -> usize { 2 }
    
    let _ =  async {
    let x = join!(one(), two()).await;
    assert_eq!(x, (1, 2));
    };
}
