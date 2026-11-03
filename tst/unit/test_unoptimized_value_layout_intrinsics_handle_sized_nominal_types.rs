//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]

struct Aligned { first: u8, second: u64 }
enum Choice { Empty, Value(u64) }
union Bits { words: [u32; 2], number: u64 }
struct Tail<T: ?Sized>(u16, T);

#[inline(never)]
fn layout<T: ?Sized>(value: &T) -> (usize, usize) {
    unsafe {
        (core::intrinsics::size_of_val(value), core::intrinsics::align_of_val(value))
    }
}

fn main() {
    let aligned = Aligned { first: 2, second: 40 };
    let choice = Choice::Value(42);
    let bits = Bits { number: 42 };
    assert_eq!(layout(&aligned), (std::mem::size_of::<Aligned>(), std::mem::align_of::<Aligned>()));
    assert_eq!(layout(&choice), (std::mem::size_of::<Choice>(), std::mem::align_of::<Choice>()));
    assert_eq!(layout(&bits), (std::mem::size_of::<Bits>(), std::mem::align_of::<Bits>()));
    let nested = Tail(7, Aligned { first: 1, second: 2 });
    assert_eq!(layout(&nested), (std::mem::size_of_val(&nested), std::mem::align_of_val(&nested)));
    let wide: &Tail<[u8]> = &Tail(3, [5, 7, 11]);
    assert_eq!(layout(wide), (6, 2));
    assert_eq!(u64::from(aligned.first) + aligned.second, 42);
    assert!(matches!(choice, Choice::Value(42)));
    assert_eq!(unsafe { bits.number }, 42);
}
