// Extracted from src/trait-bounds.md:134
#![allow(unused)]
fn main() {
    struct T;
    impl<'a> PartialEq<i32> for &'a T {
        // ...
       fn eq(&self, other: &i32) -> bool {true}
    }
}
