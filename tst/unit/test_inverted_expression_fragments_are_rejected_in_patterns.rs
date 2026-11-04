//@ compile-fail: arbitrary expressions
macro_rules! classify {
    ($pattern:expr) => {
        fn main() { match 248u8 { $pattern => (), _ => () } }
    };
}
classify!(!7u8);
