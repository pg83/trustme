// Extracted from library/core/src/error.rs:408
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {
    use core::error::Error;
    use core::error::request_ref;
    
    fn get_str(err: &impl Error) -> &str {
        request_ref::<str>(err).unwrap()
    }
}
