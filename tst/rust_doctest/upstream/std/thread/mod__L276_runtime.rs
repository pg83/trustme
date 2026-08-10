// Extracted from library/std/src/thread/mod.rs:276
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new()
                                  .name("foo".into())
                                  .stack_size(32 * 1024);

    let handler = builder.spawn(|| {
        // thread code
    }).unwrap();

    handler.join().unwrap();
}
