#![feature(lang_items)]

macro_rules! assert {
    ($cond:expr $(,)?) => {{ /* compiler built-in */ }};
    ($cond:expr, $($arg:tt)+) => {{ /* compiler built-in */ }};
}

fn _test() {
    let _: () = assert!(true);
    let _: () = assert!(true);
}
