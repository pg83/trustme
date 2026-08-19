//@ run-pass
// The crate's entry point may be a `use`, which names the function that runs.
// Looking a value up by its absolute path follows the import to the item
// behind it, so both the entry point and an ordinary call reach the function.

pub mod inner {
    pub fn real_main() {
        println!("ran");
    }

    pub const ANSWER: u32 = 42;
}

use inner::real_main as main;

pub use inner::ANSWER as REEXPORTED;

const _: () = assert!(REEXPORTED == 42);
