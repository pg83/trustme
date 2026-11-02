//@ compile-fail: not allowed for

macro_rules! parse_words {
    ($value:expr $( $word:ident )+) => { () };
}
fn main() {}
