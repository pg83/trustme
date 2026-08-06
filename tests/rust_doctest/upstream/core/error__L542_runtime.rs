// Extracted from library/core/src/error.rs:542
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {
    
    use core::error::Request;
    
    #[derive(Debug)]
    struct SomeConcreteType { field: u8 }
    
    impl std::fmt::Display for SomeConcreteType {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "{} failed", self.field)
        }
    }
    
    impl std::error::Error for SomeConcreteType {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            request.provide_value::<u8>(self.field);
        }
    }
}
