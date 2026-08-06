// Extracted from library/core/src/fmt/mod.rs:695
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        assert_eq!(format_args!("hello").as_str(), Some("hello"));
        assert_eq!(format_args!("").as_str(), Some(""));
        assert_eq!(format_args!("{:?}", std::env::current_dir()).as_str(), None);
        Ok(())
    }
    doctest().unwrap();
}
