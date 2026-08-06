// Extracted from src/paths.md:33
#![allow(unused)]
fn main() {
    use std::io::{self, Write};
    mod m {
        #[clippy::cyclomatic_complexity = "0"]
        pub (in super) fn f1() {}
    }
}
