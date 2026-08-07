// Extracted from library/std/src/thread/current.rs:195
#![allow(unused)]
fn main() {
    use std::thread;

    let handler = thread::Builder::new()
        .name("named thread".into())
        .spawn(|| {
            let handle = thread::current();
            assert_eq!(handle.name(), Some("named thread"));
        })
        .unwrap();

    handler.join().unwrap();
}
