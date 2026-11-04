//@ compile-fail: arbitrary expressions
const MARK: u8 = 23;
macro_rules! classify {
    ($pattern:expr) => {
        fn main() { match 23u8 { $pattern => (), _ => () } }
    };
}
classify!(MARK);
