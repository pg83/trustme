// `async Fn(..)` is the async callable trait of the same shape, whose path only
// becomes writable once the core crate is known.
//@ edition: 2021
#![feature(async_trait_bounds)]

async fn call_asyncly(f: impl async Fn(i32) -> i32) -> i32 {
    f(1).await
}

async fn call_once(f: impl async FnOnce(i32) -> i32) -> i32 {
    f(2).await
}

fn main() {
    let fut = call_asyncly(|x| async move { x + 1 });
    let once = call_once(|x| async move { x + 10 });
    let _ = (fut, once);
}
