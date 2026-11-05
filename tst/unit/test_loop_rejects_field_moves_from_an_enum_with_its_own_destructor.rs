//@ compile-fail: error
struct Token;
impl Drop for Token { fn drop(&mut self) {} }
enum Owned { Full(Token), Empty }
impl Drop for Owned { fn drop(&mut self) {} }
fn make() -> Owned { Owned::Full(Token) }
fn partial(mut value: Owned) {
    let mut step = 0;
    loop {
        value = make();
        match value { Owned::Full(token) => drop(token), Owned::Empty => {} }
        step += 1;
        if step == 2 { break; }
    }
}
fn maybe_partial(mut value: Owned) {
    let mut step = 0;
    loop {
        value = make();
        if std::hint::black_box(step == 0) {
            match value { Owned::Full(token) => drop(token), Owned::Empty => {} }
        } else {
            drop(value);
        }
        step += 1;
        if step == 2 { break; }
    }
}
fn main() {
    partial(make());
    maybe_partial(make());
}
