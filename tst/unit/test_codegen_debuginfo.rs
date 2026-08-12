//@ compile-flags: -Cdebuginfo=2

fn main() {
    let value = 42_u32;
    assert_eq!(value, 42);
}
