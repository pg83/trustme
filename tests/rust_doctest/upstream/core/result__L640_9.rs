// Extracted from library/core/src/result.rs:640
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io::{Error, ErrorKind};
        
        let x: Result<u32, Error> = Err(Error::new(ErrorKind::NotFound, "!"));
        assert_eq!(x.is_err_and(|x| x.kind() == ErrorKind::NotFound), true);
        
        let x: Result<u32, Error> = Err(Error::new(ErrorKind::PermissionDenied, "!"));
        assert_eq!(x.is_err_and(|x| x.kind() == ErrorKind::NotFound), false);
        
        let x: Result<u32, Error> = Ok(123);
        assert_eq!(x.is_err_and(|x| x.kind() == ErrorKind::NotFound), false);
        
        let x: Result<u32, String> = Err("ownership".to_string());
        assert_eq!(x.as_ref().is_err_and(|x| x.len() > 1), true);
        println!("still alive {:?}", x);
        Ok(())
    }
    doctest().unwrap();
}
