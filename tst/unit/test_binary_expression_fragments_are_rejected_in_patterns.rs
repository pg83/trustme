//@ compile-fail: arbitrary expressions
macro_rules! classify {
    ($pattern:expr) => {
        fn main() { match 2u8 { $pattern => (), _ => () } }
    };
}
classify!(1u8 + 1u8);
