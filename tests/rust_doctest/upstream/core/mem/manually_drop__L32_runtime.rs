// Extracted from library/core/src/mem/manually_drop.rs:32
#![allow(unused)]
fn main() {
    struct Context;

    struct Widget {
        children: Vec<Widget>,
        // `context` will be dropped after `children`.
        // Rust guarantees that fields are dropped in the order of declaration.
        context: Context,
    }
}
