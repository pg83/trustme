// Extracted from library/core/src/mem/drop_guard.rs:12
#![allow(unused)]
#![allow(unused)]
#![feature(drop_guard)]
fn main() {

    use std::mem::DropGuard;

    {
        // Create a new guard around a string that will
        // print its value when dropped.
        let s = String::from("Chashu likes tuna");
        let mut s = DropGuard::new(s, |s| println!("{s}"));

        // Modify the string contained in the guard.
        s.push_str("!!!");

        // The guard will be dropped here, printing:
        // "Chashu likes tuna!!!"
    }
}
