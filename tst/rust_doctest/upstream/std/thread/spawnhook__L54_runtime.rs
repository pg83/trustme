// Extracted from library/std/src/thread/spawnhook.rs:54
#![allow(unused)]
#![feature(thread_spawn_hook)]
fn main() {

    std::thread::add_spawn_hook(|_| {
        ..; // This will run in the parent (spawning) thread.
        move || {
            ..; // This will run it the child (spawned) thread.
        }
    });
}
