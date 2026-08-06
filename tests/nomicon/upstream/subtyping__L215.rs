// Extracted from src/subtyping.md:215
#![allow(unused)]
fn main() {
    fn assign<T>(input: &mut T, val: T) {
        *input = val;
    }
}
