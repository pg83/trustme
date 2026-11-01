//@ compile-fail: Duplicate definition of #[main]
#[main]
fn first() {}
#[main]
fn second() {}
fn main() {}
