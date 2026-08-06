// Extracted from library/std/src/io/error.rs:635
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io::Error;
        
        let os_error = Error::last_os_error();
        println!("last OS error: {os_error:?}");
        Ok(())
    }
    doctest().unwrap();
}
