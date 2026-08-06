// Extracted from library/core/src/fmt/builders.rs:1219
#![allow(unused)]
#![feature(debug_closure_helpers)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        let value = 'a';
        assert_eq!(format!("{}", value), "a");
        assert_eq!(format!("{:?}", value), "'a'");
        
        let wrapped = fmt::from_fn(|f| write!(f, "{value:?}"));
        assert_eq!(format!("{}", wrapped), "'a'");
        assert_eq!(format!("{:?}", wrapped), "'a'");
        Ok(())
    }
    doctest().unwrap();
}
