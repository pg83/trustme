//@ edition: 2024

// A closure whose body diverges has result type `!`. From edition 2024 the
// never type no longer falls back to `()`, so an inference variable that only
// such a body constrains has to settle on `!` itself. Leaving it unresolved
// carries it all the way into code generation, where a type variable cannot be
// mangled.

use std::thread;

fn main() {
    let handle = thread::spawn(|| panic!("expected"));
    assert!(handle.join().is_err());

    let discarded = thread::spawn(move || {
        panic!("expected");
    })
    .join();
    assert!(discarded.is_err());
}
