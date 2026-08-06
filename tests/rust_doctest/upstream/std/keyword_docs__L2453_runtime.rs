// Extracted from library/std/src/keyword_docs.rs:2453
#![allow(unused)]
fn main() {
    #[expect(unused_variables)]
    fn example() -> i32 {
        let x = {
            return 5;
        };
    }
}
