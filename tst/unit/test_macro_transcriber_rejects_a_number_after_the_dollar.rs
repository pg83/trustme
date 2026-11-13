//@ compile-fail: error:

macro_rules! invalid {
    () => { $7 };
}

fn main() {}
