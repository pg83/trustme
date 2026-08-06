// Extracted from library/core/src/error.rs:385
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {
    use std::error::Error;
    use core::error::request_value;
    
    fn get_string(err: &impl Error) -> String {
        request_value::<String>(err).unwrap()
    }
}
