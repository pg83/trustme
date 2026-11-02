//@ compile-fail: Type mismatch between
fn main() {
    let pointer = &[61, 67, 71] as *const [u16];
    assert_eq!(unsafe { &*pointer }, &[61u16, 67, 71]);
}
