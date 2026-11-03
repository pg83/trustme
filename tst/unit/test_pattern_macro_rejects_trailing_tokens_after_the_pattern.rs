//@ compile-fail: Trailing tokens in macro expansion
macro_rules! invalid { () => { 1, 2 }; }

fn main() {
    match 1 { invalid!() => (), _ => () }
}
