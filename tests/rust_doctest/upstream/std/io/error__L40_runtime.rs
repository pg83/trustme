// Extracted from library/std/src/io/error.rs:40
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        
        fn get_string() -> io::Result<String> {
            let mut buffer = String::new();
        
            io::stdin().read_line(&mut buffer)?;
        
            Ok(buffer)
        }
        Ok(())
    }
    doctest().unwrap();
}
