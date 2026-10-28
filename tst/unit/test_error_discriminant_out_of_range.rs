//@ compile-fail: error
#[repr(u8)]
enum E {
    A = 256,
}
fn main() {}
