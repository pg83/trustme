// Extracted from library/core/src/option.rs:101
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        fn add_last_numbers(stack: &mut Vec<i32>) -> Option<i32> {
            Some(stack.pop()? + stack.pop()?)
        }
        Ok(())
    }
    doctest().unwrap();
}
