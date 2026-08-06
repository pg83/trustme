// Extracted from library/core/src/macros/mod.rs:950
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let debug = format!("{:?}", format_args!("{} foo {:?}", 1, 2));
        let display = format!("{}", format_args!("{} foo {:?}", 1, 2));
        assert_eq!("1 foo 2", display);
        assert_eq!(display, debug);
        Ok(())
    }
    doctest().unwrap();
}
