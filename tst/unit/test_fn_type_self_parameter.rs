// A function type has no receiver, but the grammar still lets `self` appear in
// its parameter list — rustc rejects it after parsing, which a `cfg` removes
// the item before reaching. The parser stopped at the `mut`.
//
// Same shape as the ui test parser/self-param-syntactic-pass.rs.
struct X;
trait Y {
    type X;
}

#[cfg(FALSE)]
impl Y for X {
    type X = fn(self);
    type X = fn(mut self);
    type X = fn(&self);
    type X = fn(&mut self);
    type X = fn(&'a self);
    type X = fn(self: u8);
}

impl Y for X {
    type X = fn(u8) -> u8;
}

fn main() {
    let f: <X as Y>::X = |v| v + 1;
    assert_eq!(f(1), 2);
}
