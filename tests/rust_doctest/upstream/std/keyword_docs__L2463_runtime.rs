// Extracted from library/std/src/keyword_docs.rs:2463
#![allow(unused)]
fn main() {
    async fn example() -> i32 {
        let x = async {
            return 5;
        };

        x.await
    }
}
