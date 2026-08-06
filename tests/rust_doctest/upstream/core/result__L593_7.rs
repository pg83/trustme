// Extracted from library/core/src/result.rs:593
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let x: Result<u32, &str> = Ok(2);
        assert_eq!(x.is_ok_and(|x| x > 1), true);
        
        let x: Result<u32, &str> = Ok(0);
        assert_eq!(x.is_ok_and(|x| x > 1), false);
        
        let x: Result<u32, &str> = Err("hey");
        assert_eq!(x.is_ok_and(|x| x > 1), false);
        
        let x: Result<String, &str> = Ok("ownership".to_string());
        assert_eq!(x.as_ref().is_ok_and(|x| x.len() > 1), true);
        println!("still alive {:?}", x);
        Ok(())
    }
    doctest().unwrap();
}
