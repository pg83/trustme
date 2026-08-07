// Extracted from library/core/src/panic/location.rs:217
#![allow(unused)]
fn main() {
    use std::panic;

    panic::set_hook(Box::new(|panic_info| {
        if let Some(location) = panic_info.location() {
            println!("panic occurred at line {}", location.line());
        } else {
            println!("panic occurred but can't get location information...");
        }
    }));

    panic!("Normal panic");
}
