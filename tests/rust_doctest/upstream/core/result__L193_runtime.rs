// Extracted from library/core/src/result.rs:193
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs::File;
        use std::io::prelude::*;
        use std::io;
        
        struct Info {
            name: String,
            age: i32,
            rating: i32,
        }
        
        fn write_info(info: &Info) -> io::Result<()> {
            let mut file = File::create("my_best_friends.txt")?;
            // Early return on error
            file.write_all(format!("name: {}\n", info.name).as_bytes())?;
            file.write_all(format!("age: {}\n", info.age).as_bytes())?;
            file.write_all(format!("rating: {}\n", info.rating).as_bytes())?;
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
