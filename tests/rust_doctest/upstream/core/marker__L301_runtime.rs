// Extracted from library/core/src/marker.rs:301
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // We can derive a `Copy` implementation. `Clone` is also required, as it's
        // a supertrait of `Copy`.
        #[derive(Debug, Copy, Clone)]
        struct Foo;
        
        let x = Foo;
        
        let y = x;
        
        // `y` is a copy of `x`
        
        println!("{x:?}"); // A-OK!
        Ok(())
    }
    doctest().unwrap();
}
