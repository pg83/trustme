// Extracted from library/core/src/option.rs:428
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        fn make_iter(do_insert: bool) -> impl Iterator<Item = i32> {
            // Explicit returns to illustrate return types matching
            match do_insert {
                true => return (0..4).chain(Some(42)).chain(4..8),
                false => return (0..4).chain(None).chain(4..8),
            }
        }
        println!("{:?}", make_iter(true).collect::<Vec<_>>());
        println!("{:?}", make_iter(false).collect::<Vec<_>>());
        Ok(())
    }
    doctest().unwrap();
}
