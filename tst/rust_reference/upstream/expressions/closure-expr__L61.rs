// Extracted from src/expressions/closure-expr.md:61
#![allow(unused)]
fn main() {
    async fn takes_async_callback(f: impl AsyncFn(u64)) {
        f(0).await;
        f(1).await;
    }
    
    async fn example() {
        takes_async_callback(async |i| {
            core::future::ready(i).await;
            println!("done with {i}.");
        }).await;
    }
}
