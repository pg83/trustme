//@ compile-flags: -Coverflow-checks=off
fn shifts() -> (u16, i16, u16, i16) {
    let unsigned = 0x8123u16;
    let signed = -32509i16;
    let left = 3u8;
    let right = 3i8;
    (unsigned << left, signed << left, unsigned >> right, signed >> right)
}
fn main() { assert_eq!(shifts(), (2328, 2072, 4132, -4064)); }
