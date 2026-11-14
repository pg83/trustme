//@ proc-macro-aux-build: cov99_w439_unwrap_tokens.rs

macro_rules! leaf {
    () => { 2usize };
}

macro_rules! split {
    ($expression:expr) => {{
        let text = cov99_w439_unwrap_tokens::unwrap_tokens!($expression);
        const EARLY: usize = $expression;
        (text, EARLY)
    }};
}

macro_rules! forward {
    ($value:expr) => { split!($value + 1usize) };
}

fn main() {
    let (text, number) = forward!(leaf!());
    assert_eq!(number, 3);
    assert!(!text.is_empty());
}
