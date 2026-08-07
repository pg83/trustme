// Extracted from library/std/src/panic.rs:30
#![allow(unused)]
fn main() {
    use std::panic;

    panic::set_hook(Box::new(|panic_info| {
        println!("panic occurred: {panic_info}");
    }));

    panic!("critical system failure");
}
