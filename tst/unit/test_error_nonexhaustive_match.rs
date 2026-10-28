//@ compile-fail: error
enum E {
    A,
    B,
}
fn check(value: E) {
    match value {
        E::A => {}
    }
}
fn main() {}
