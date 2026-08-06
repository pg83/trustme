// Extracted from library/alloc/src/fmt.rs:486
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        assert_eq!(format!("{} {:?}", 3, 4), "3 4");
        assert_eq!(format!("{} {:?}", 'a', 'b'), "a 'b'");
        assert_eq!(format!("{} {:?}", "foo\n", "bar\n"), "foo\n \"bar\\n\"");
        Ok(())
    }
    doctest().unwrap();
}
