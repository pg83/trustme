// Extracted from library/core/src/result.rs:160
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    use std::fs::File;
    use std::io::prelude::*;
    use std::io;
    
    struct Info {
        name: String,
        age: i32,
        rating: i32,
    }
    
    fn write_info(info: &Info) -> io::Result<()> {
        // Early return on error
        let mut file = match File::create("my_best_friends.txt") {
               Err(e) => return Err(e),
               Ok(f) => f,
        };
        if let Err(e) = file.write_all(format!("name: {}\n", info.name).as_bytes()) {
            return Err(e)
        }
        if let Err(e) = file.write_all(format!("age: {}\n", info.age).as_bytes()) {
            return Err(e)
        }
        if let Err(e) = file.write_all(format!("rating: {}\n", info.rating).as_bytes()) {
            return Err(e)
        }
        Ok(())
    }
}
