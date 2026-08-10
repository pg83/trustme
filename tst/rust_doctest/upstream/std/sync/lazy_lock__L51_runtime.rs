// Extracted from library/std/src/sync/lazy_lock.rs:51
use std::sync::LazyLock;

#[derive(Debug)]
struct UseCellLock {
    number: LazyLock<u32>,
}
fn main() {
    let lock: LazyLock<u32> = LazyLock::new(|| 0u32);

    let data = UseCellLock { number: lock };
    println!("{}", *data.number);
}
