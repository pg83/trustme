//@ compile-fail: is already used for a generic parameter

trait Outer<'a> {
    fn shadow<'a>();
}

fn main() {}
