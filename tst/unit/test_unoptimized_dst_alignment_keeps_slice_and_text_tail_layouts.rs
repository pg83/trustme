//@ compile-variant: -Zmir-opt-level=0
#[repr(C)]
struct Tail<T: ?Sized> { head: u32, value: T }
#[inline(never)]
fn alignment<T: ?Sized>(value: &T) -> usize { core::mem::align_of_val(value) }
fn main() {
    let words = [487u64, 491, 499];
    assert_eq!(alignment(&words[..]), core::mem::align_of::<u64>());
    assert_eq!(alignment("éøß"), 1);
    let owned = Tail { head: 503, value: words };
    let sliced: &Tail<[u64]> = &owned;
    assert_eq!(alignment(sliced), core::mem::align_of::<Tail<[u64; 3]>>());
    assert_eq!(sliced.value[1], 491);
    let text = Tail { head: 509, value: *b"abc" };
    let bytes: &Tail<[u8]> = &text;
    assert_eq!(alignment(bytes), core::mem::align_of::<u32>());
}
