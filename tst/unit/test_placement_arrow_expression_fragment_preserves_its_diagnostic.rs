//@ compile-fail: No macro arm matched

macro_rules! reject {
    ($expression:expr) => { compile_error!(stringify!($expression)); };
}

reject!(23 <- 17);

fn main() {}
