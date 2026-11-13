use std::future::Future;

fn future_factory() -> impl Future<Output = u32> {
    let _recur = || future_factory();
    async { 44 }
}

fn main() {
    drop(future_factory());
}
