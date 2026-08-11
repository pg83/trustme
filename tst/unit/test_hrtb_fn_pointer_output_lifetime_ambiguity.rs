//@ compile-fail: Unspecified lifetime in outer context

type Ambiguous = for<'a> fn(&'a u8, &'a u8) -> &u8;

fn main() {}
