// Extracted from src/macro-ambiguity.md:32
#![allow(unused)]
fn main() {
    macro_rules! i_am_an_mbe {
        (start $foo:expr $($i:ident),* end) => ($foo)
    }
}
