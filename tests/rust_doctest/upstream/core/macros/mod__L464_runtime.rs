// Extracted from library/core/src/macros/mod.rs:464
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        use std::fs::File;
        use std::io::prelude::*;
        
        enum MyError {
            FileWriteError
        }
        
        impl From<io::Error> for MyError {
            fn from(e: io::Error) -> MyError {
                MyError::FileWriteError
            }
        }
        
        // The preferred method of quick returning Errors
        fn write_to_file_question() -> Result<(), MyError> {
            let mut file = File::create("my_best_friends.txt")?;
            file.write_all(b"This is a list of my best friends.")?;
            Ok(())
        }
        
        // The previous method of quick returning Errors
        fn write_to_file_using_try() -> Result<(), MyError> {
            let mut file = r#try!(File::create("my_best_friends.txt"));
            r#try!(file.write_all(b"This is a list of my best friends."));
            Ok(())
        }
        
        // This is equivalent to:
        fn write_to_file_using_match() -> Result<(), MyError> {
            let mut file = r#try!(File::create("my_best_friends.txt"));
            match file.write_all(b"This is a list of my best friends.") {
                Ok(v) => v,
                Err(e) => return Err(From::from(e)),
            }
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
