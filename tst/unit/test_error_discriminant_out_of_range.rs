//@ compile-fail: is not in the range of the enum's tag type
#[repr(u8)]
enum E {
    A = 255,
    B,
}
fn main() {}
