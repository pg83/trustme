// Extracted from library/std/src/io/mod.rs:214
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        
        fn read_input() -> io::Result<()> {
            let mut input = String::new();
        
            io::stdin().read_line(&mut input)?;
        
            println!("You typed: {}", input.trim());
        
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
