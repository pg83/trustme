//@ compile-fail: arbitrary expressions
const LIMIT: i16 = 11;
macro_rules! classify {
    ($pattern:expr) => {
        fn main() { match -11i16 { $pattern => (), _ => () } }
    };
}
classify!(-LIMIT);
