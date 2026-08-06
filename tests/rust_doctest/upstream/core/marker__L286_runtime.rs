// Extracted from library/core/src/marker.rs:286
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        #[derive(Debug)]
        struct Foo;
        
        let x = Foo;
        
        let y = x;
        
        // `x` has moved into `y`, and so cannot be used
        
        // println!("{x:?}"); // error: use of moved value
        Ok(())
    }
    doctest().unwrap();
}
