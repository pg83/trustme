// Extracted from library/std/src/panic.rs:76
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::panic;
        
        panic::set_hook(Box::new(|panic_info| {
            if let Some(s) = panic_info.payload().downcast_ref::<&str>() {
                println!("panic occurred: {s:?}");
            } else if let Some(s) = panic_info.payload().downcast_ref::<String>() {
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
