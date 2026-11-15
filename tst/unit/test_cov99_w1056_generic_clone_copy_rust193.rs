//@ env: RUSTC_OVERRIDE_VERSION_STRING=1.93.0
//@ compile-flags: -C panic=abort

#[derive(Clone, Copy)]
struct Packet<T>(T);

fn main() {
    let original = Packet(37u32);
    let copied = original;
    let cloned = original.clone();
    assert_eq!(copied.0, 37);
    assert_eq!(cloned.0, 37);
}
