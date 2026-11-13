//@ compile-fail: error:

macro_rules! missing_separator {
    () => {}
    (next) => {}
}

fn main() {}
