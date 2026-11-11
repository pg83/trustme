//@ compile-fail: error

fn cyclic() -> impl Sized {
    (cyclic(),)
}

fn main() {}
