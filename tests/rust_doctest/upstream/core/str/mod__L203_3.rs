// Extracted from library/core/src/str/mod.rs:203
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // some bytes, in a vector
        let sparkle_heart = vec![240, 159, 146, 150];
        
        // We can use the ? (try) operator to check if the bytes are valid
        let sparkle_heart = str::from_utf8(&sparkle_heart)?;
        
        assert_eq!("💖", sparkle_heart);
        Ok::<_, std::str::Utf8Error>(())
    }
    doctest().unwrap();
}
