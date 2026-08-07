// Extracted from library/std/src/thread/scoped.rs:303
#![allow(unused)]
fn main() {
    use std::thread;

    thread::scope(|s| {
        let t = s.spawn(|| {
            panic!("oh no");
        });
        assert!(t.join().is_err());
    });
}
