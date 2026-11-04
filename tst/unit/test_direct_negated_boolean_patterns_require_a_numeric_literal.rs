//@ compile-fail: Unexpected token
fn main() {
    match false { -true => (), _ => () }
}
