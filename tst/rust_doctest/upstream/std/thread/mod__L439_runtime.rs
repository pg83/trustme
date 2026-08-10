// Extracted from library/std/src/thread/mod.rs:439
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new();

    let x = 1;
    let thread_x = &x;

    let handler = unsafe {
        builder.spawn_unchecked(move || {
            println!("x = {}", *thread_x);
        }).unwrap()
    };

    // caller has to ensure `join()` is called, otherwise
    // it is possible to access freed memory if `x` gets
    // dropped before the thread closure is executed!
    handler.join().unwrap();
}
