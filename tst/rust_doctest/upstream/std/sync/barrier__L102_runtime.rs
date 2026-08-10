// Extracted from library/std/src/sync/barrier.rs:102
#![allow(unused)]
fn main() {
    use std::sync::Barrier;
    use std::thread;

    let n = 10;
    let barrier = Barrier::new(n);
    thread::scope(|s| {
        for _ in 0..n {
            // The same messages will be printed together.
            // You will NOT see any interleaving.
            s.spawn(|| {
                println!("before wait");
                barrier.wait();
                println!("after wait");
            });
        }
    });
}
