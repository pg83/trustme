//@ compile-flags: -Zmir-opt-level=3
#[repr(align(32))]
struct Aligned(u8);
#[repr(align(64))]
struct Dropped<'a>(&'a std::cell::Cell<u8>);
impl Drop for Dropped<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
trait Value { fn value(&self) -> u8; }
impl Value for Aligned { fn value(&self) -> u8 { self.0 } }
fn alignment(value: &dyn Value) -> usize { std::mem::align_of_val(value) }
fn main() {
    let value = Aligned(7);
    assert_eq!(std::mem::align_of_val(&value), 32);
    assert_eq!(std::mem::align_of_val(&3u16), 2);
    let bytes: &[u8] = &[3, 5, 7];
    assert_eq!(std::mem::align_of_val(bytes), 1);
    assert_eq!(alignment(&value), 32);
    assert_eq!((&value as &dyn Value).value(), 7);
    let drops = std::cell::Cell::new(0u8);
    assert_eq!(std::mem::align_of_val(&Dropped(&drops)), 64);
    assert_eq!(drops.get(), 1);
}
