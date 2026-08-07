// Extracted from library/std/src/thread/local.rs:54
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    use std::thread;

    // explicit `const {}` block enables more efficient initialization
    thread_local!(static FOO: Cell<u32> = const { Cell::new(1) });

    assert_eq!(FOO.get(), 1);
    FOO.set(2);

    // each thread starts out with the initial value of 1
    let t = thread::spawn(move || {
        assert_eq!(FOO.get(), 1);
        FOO.set(3);
    });

    // wait for the thread to complete and bail out on panic
    t.join().unwrap();

    // we retain our original value of 2 despite the child thread
    assert_eq!(FOO.get(), 2);
}
