// Extracted from library/std/src/thread/spawnhook.rs:69
#![allow(unused)]
#![feature(thread_spawn_hook)]
fn main() {

    use std::cell::Cell;

    thread_local! {
        static X: Cell<u32> = Cell::new(0);
    }

    // This needs to be done once in the main thread before spawning any threads.
    std::thread::add_spawn_hook(|_| {
        // Get the value of X in the spawning thread.
        let value = X.get();
        // Set the value of X in the newly spawned thread.
        move || X.set(value)
    });

    X.set(123);

    std::thread::spawn(|| {
        assert_eq!(X.get(), 123);
    }).join().unwrap();
}
