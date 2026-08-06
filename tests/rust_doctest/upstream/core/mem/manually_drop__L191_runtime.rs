// Extracted from library/core/src/mem/manually_drop.rs:191
#![allow(unused)]
fn main() {
    use std::mem::ManuallyDrop;
    let x = ManuallyDrop::new(Box::new(()));
    let _: Box<()> = ManuallyDrop::into_inner(x); // This drops the `Box`.
}
