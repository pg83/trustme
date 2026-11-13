use std::future::Future;
use std::ops::AsyncFnOnce;

fn deferred<F, A, R>(call: F, value: A) -> impl Future<Output = R>
where
    F: AsyncFnOnce(A) -> R,
{
    async move { call(value).await }
}

fn main() {
    let future = deferred(async |value| value, 13u8);
    drop(future);
}
