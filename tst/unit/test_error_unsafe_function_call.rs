//@ compile-fail: error
unsafe fn dangerous() {}
fn main() {
    dangerous();
}
