//@ compile-variant: -O
#![feature(generic_const_exprs, ptr_metadata)]
#![allow(incomplete_features)]

fn inspect<const N: usize>() where [(); N + 1]: {
    let values = [0x1234u16; N + 1];
    let index = std::hint::black_box(N);
    assert_eq!(values[index], 0x1234);
    let pointer: *const [u16; N + 1] = &values;
    let slice: *const [u16] = pointer;
    assert_eq!(std::ptr::metadata(slice), N + 1);
    assert_eq!(unsafe { &*slice }.len(), N + 1);
    assert_eq!(std::mem::size_of_val(unsafe { &*slice }), (N + 1) * 2);
    let zero_sized = [(); N + 1];
    let empty_payload: &[()] = &zero_sized;
    assert_eq!(empty_payload.len(), N + 1);
    assert_eq!(std::mem::size_of_val(empty_payload), 0);
    let nested = [[7u8; N + 1]; 2];
    let outer: &[[u8; N + 1]] = &nested;
    assert_eq!(outer.len(), 2);
    assert_eq!(outer[1][index], 7);
}

fn main() {
    inspect::<0>();
    inspect::<1>();
    inspect::<9>();
}
