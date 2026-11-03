//@ native-static-aux-build: orchard_native_ops.rs
//@ compile-flags: -Lframework=$OUT.framework -Clink-arg=-Wl,--build-id=none
#[link(name = "orchard_native_ops", kind = "static")]
extern "C" { fn orchard_update(slot: *mut u32, increment: u32) -> u32; }

fn main() {
    let mut value = 227u32;
    let before = unsafe { orchard_update(&mut value, 17) };
    assert_eq!(before, 1816);
    assert_eq!(value, 244);
}
