//@ compile-fail: Failed to find impl with 'Missing'
enum E {
    A,
    B,
}
fn check(value: E) {
    match value {
        E::Missing => {}
    }
}
fn main() {}
