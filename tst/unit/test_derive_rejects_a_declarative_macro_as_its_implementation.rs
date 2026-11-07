//@ compile-fail: error:

macro_rules! Counted {
    () => { 437u16 };
}

#[derive(Counted)]
struct Entry;

fn main() {}
