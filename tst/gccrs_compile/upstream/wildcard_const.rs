#![feature(lang_items)]

macro_rules! assert {
    ($cond:expr $(,)?) => {{ /* compiler built-in */ }};
    ($cond:expr, $($arg:tt)+) => {{ /* compiler built-in */ }};
}

const _: () = assert!(true);
const _: () = assert!(true);
