//@ proc-macro-aux-build: cov99_w439_log_tokens.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-proc
#![feature(log_syntax)]

macro_rules! leaf {
    () => { 2usize };
}

macro_rules! split {
    ($expression:expr) => {{
        let logged = cov99_w439_log_tokens::log_tokens!($expression);
        const EARLY: usize = $expression;
        (logged, EARLY)
    }};
}

macro_rules! forward {
    ($value:expr) => { split!($value + 1usize) };
}

fn main() {
    let (logged, number) = forward!(leaf!());
    assert_eq!((logged, number), (7, 3));
}
