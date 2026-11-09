//@ compile-fail: Expected a string literal
fn main() {
    let _ = env!(false);
}
