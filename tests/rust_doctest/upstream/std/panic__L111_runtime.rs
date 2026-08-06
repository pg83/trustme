// Extracted from library/std/src/panic.rs:111
#![allow(unused)]
#![feature(panic_payload_as_str)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        std::panic::set_hook(Box::new(|panic_info| {
            if let Some(s) = panic_info.payload_as_str() {
                println!("panic occurred: {s:?}");
            } else {
                println!("panic occurred");
            }
        }));
        
        panic!("Normal panic");
        Ok(())
    }
    doctest().unwrap();
}
