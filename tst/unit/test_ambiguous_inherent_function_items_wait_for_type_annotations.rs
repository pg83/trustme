//@ reject-flags: --cfg ambiguous => type annotations needed || multiple applicable items in scope
struct Marker<T>(T);
impl Marker<u8> { fn read() -> u16 { 103 } }
impl Marker<u16> { fn read() -> u16 { 107 } }
#[cfg(ambiguous)]
fn main() { let _ = Marker::<_>::read; }
#[cfg(not(ambiguous))]
fn main() {
    let small: fn() -> u16 = Marker::<u8>::read;
    let wide: fn() -> u16 = Marker::<u16>::read;
    assert_eq!((small(), wide()), (103, 107));
}
