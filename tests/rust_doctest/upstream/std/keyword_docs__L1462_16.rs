// Extracted from library/std/src/keyword_docs.rs:1462
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        static FOO: [i32; 5] = [1, 2, 3, 4, 5];
        
        let r1 = &FOO as *const _;
        let r2 = &FOO as *const _;
        // With a strictly read-only static, references will have the same address
        assert_eq!(r1, r2);
        // A static item can be used just like a variable in many cases
        println!("{FOO:?}");
        Ok(())
    }
    doctest().unwrap();
}
