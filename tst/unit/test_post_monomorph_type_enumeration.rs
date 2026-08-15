use std::future::Future;
use std::pin::Pin;

trait Unit {}

impl Unit for () {}

fn recurse() -> Pin<Box<dyn Future<Output = impl Unit>>> {
    Box::pin(async {
        recurse().await;
    })
}

fn main() {
    let _ = recurse();
}
