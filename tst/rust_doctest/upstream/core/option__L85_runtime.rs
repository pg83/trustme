// Extracted from library/core/src/option.rs:85
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    fn add_last_numbers(stack: &mut Vec<i32>) -> Option<i32> {
        let a = stack.pop();
        let b = stack.pop();

        match (a, b) {
            (Some(x), Some(y)) => Some(x + y),
            _ => None,
        }
    }
}
