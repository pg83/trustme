// derive-where's tests order `#[repr(C, u8)] enum Test<T> { A(Wrapper<T>) = 1,
// B = 0, C = 2 }` by reading the tag through a pointer. A `repr(C)` enum with
// fields is laid out as a `repr(C)` struct of its tag (the named integer) and a
// `repr(C)` union of the variants' fields (RFC 2195); an explicit discriminant,
// which such an enum may carry once it names the tag's integer (E0732), sets
// that tag's value and nothing else. The layout asserted there was none.
#[repr(C, u8)]
enum Small<T> {
    A(T) = 1,
    B = 0,
    C = 2,
}

#[repr(C, u32)]
enum Wide {
    A(u64) = 7,
    B,
    C { x: u8 } = 3,
}

fn small_tag<T>(value: &Small<T>) -> u8 {
    unsafe { *(value as *const Small<T> as *const u8) }
}

fn wide_tag(value: &Wide) -> u32 {
    unsafe { *(value as *const Wide as *const u32) }
}

fn main() {
    let a = Small::A(42u16);
    assert_eq!(small_tag(&a), 1);
    assert_eq!(small_tag(&Small::<u16>::B), 0);
    assert_eq!(small_tag(&Small::<u16>::C), 2);
    assert!(matches!(a, Small::A(42)));

    assert_eq!(wide_tag(&Wide::A(5)), 7);
    assert_eq!(wide_tag(&Wide::B), 8);
    assert_eq!(wide_tag(&Wide::C { x: 1 }), 3);
    assert_eq!(core::mem::size_of::<Wide>(), 16);
    match (Wide::C { x: 9 }) {
        Wide::C { x } => assert_eq!(x, 9),
        _ => panic!(),
    }
    if let Wide::A(v) = Wide::A(11) {
        assert_eq!(v, 11);
    }
}
