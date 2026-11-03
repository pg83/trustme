//@ compile-fail: Unknown start of token
fn main() {
    assert_eq!(stringify!(`), "`");
}
