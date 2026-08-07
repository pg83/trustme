// Extracted from library/std/src/sync/once_lock.rs:184
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::OnceLock;

    let value = OnceLock::new();

    thread::scope(|s| {
        s.spawn(|| value.set(1 + 1));

        let result = value.wait();
        assert_eq!(result, &2);
    })
}
