#![feature(const_cmp, core_intrinsics)]
#![allow(internal_features)]

const COPIED: [u8; 5] = unsafe {
    let mut bytes = [1, 2, 3, 4, 5];
    core::intrinsics::copy(bytes.as_ptr(), bytes.as_mut_ptr().add(1), 4);
    bytes
};

const COPIED_POINTER_VALUES: [u8; 3] = unsafe {
    let first = 10u8;
    let second = 20u8;
    let third = 30u8;
    let mut pointers = [&first, &second, &third];
    core::intrinsics::copy(pointers.as_ptr(), pointers.as_mut_ptr().add(1), 2);
    [*pointers[0], *pointers[1], *pointers[2]]
};

const EQUAL: bool = unsafe {
    core::intrinsics::raw_eq(&[0x1234u16, 0xabcd], &[0x1234u16, 0xabcd])
};

const UNEQUAL: bool = unsafe {
    core::intrinsics::raw_eq(&[0x1234u16, 0xabcd], &[0x1234u16, 0xabce])
};

const LESS: i32 = unsafe {
    core::intrinsics::compare_bytes(b"abc".as_ptr(), b"abd".as_ptr(), 3)
};

const GREATER: i32 = unsafe {
    core::intrinsics::compare_bytes(b"abe".as_ptr(), b"abd".as_ptr(), 3)
};

fn main() {
    assert_eq!(COPIED, [1, 1, 2, 3, 4]);
    assert_eq!(COPIED_POINTER_VALUES, [10, 10, 20]);
    assert!(EQUAL);
    assert!(!UNEQUAL);
    assert!(LESS < 0);
    assert!(GREATER > 0);
}
