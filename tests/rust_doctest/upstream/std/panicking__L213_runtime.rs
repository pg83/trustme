// Extracted from library/std/src/panicking.rs:213
#![allow(unused)]
#![feature(panic_update_hook)]
fn main() {
    use std::panic;

    // Equivalent to
    // let prev = panic::take_hook();
    // panic::set_hook(move |info| {
    //     println!("...");
    //     prev(info);
    // );
    panic::update_hook(move |prev, info| {
        println!("Print custom message and execute panic handler as usual");
        prev(info);
    });

    panic!("Custom and then normal");
}
