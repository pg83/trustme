// Extracted from library/core/src/convert/mod.rs:551
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs;
        use std::io;
        use std::num;
        
        enum CliError {
            IoError(io::Error),
            ParseError(num::ParseIntError),
        }
        
        impl From<io::Error> for CliError {
            fn from(error: io::Error) -> Self {
                CliError::IoError(error)
            }
        }
        
        impl From<num::ParseIntError> for CliError {
            fn from(error: num::ParseIntError) -> Self {
                CliError::ParseError(error)
            }
        }
        
        fn open_and_parse_file(file_name: &str) -> Result<i32, CliError> {
            let mut contents = fs::read_to_string(&file_name)?;
            let num: i32 = contents.trim().parse()?;
            Ok(num)
        }
        Ok(())
    }
    doctest().unwrap();
}
