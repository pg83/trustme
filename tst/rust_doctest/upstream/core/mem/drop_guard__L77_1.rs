// Extracted from library/core/src/mem/drop_guard.rs:77
#![allow(unused)]
#![allow(unused)]
#![feature(drop_guard)]
fn main() {

    use std::mem::DropGuard;

    let value = String::from("Nori likes chicken");
    let guard = DropGuard::new(value, |s| println!("{s}"));
    assert_eq!(DropGuard::into_inner(guard), "Nori likes chicken");
}
