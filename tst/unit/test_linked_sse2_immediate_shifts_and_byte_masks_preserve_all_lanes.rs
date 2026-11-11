#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)]
#[derive(Clone, Copy)]
struct Words([u32; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Bytes([u8; 16]);

extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.pslli.d"]
    fn left(value: Words, count: i32) -> Words;
    #[link_name = "llvm.x86.sse2.psrli.d"]
    fn right(value: Words, count: i32) -> Words;
    #[link_name = "llvm.x86.sse2.pmovmskb.128"]
    fn mask(value: Bytes) -> i32;
    #[link_name = "llvm.x86.sse2.storeu.dq"]
    fn store(destination: *mut u8, value: Bytes);
}

fn main() {
    use std::hint::black_box;

    let words = black_box(Words([1, 0x8000_0000, 0xffff_ffff, 0x1234_5678]));
    let bytes = black_box(Bytes([0, 0x80, 0x7f, 0xff, 1, 2, 3, 4, 0x80, 5, 6, 7, 8, 9, 10, 0xff]));
    unsafe {
        let left_wide = left(words, black_box(32)).0;
        let right_wide = right(words, black_box(32)).0;
        let all_bits = mask(bytes);
        let mut destination = [0x5au8; 18];
        store(destination.as_mut_ptr().add(1), bytes);
        assert_eq!((left_wide, right_wide, all_bits), ([0; 4], [0; 4], 0x810a));

        assert_eq!(&destination[1..17], &bytes.0);
        assert_eq!((destination[0], destination[17]), (0x5a, 0x5a));
        assert_eq!(left(words, 0).0, words.0);
        assert_eq!(right(words, 0).0, words.0);
        assert_eq!(left(words, 1).0, [2, 0, 0xffff_fffe, 0x2468_acf0]);
        assert_eq!(right(words, 1).0, [0, 0x4000_0000, 0x7fff_ffff, 0x091a_2b3c]);
        assert_eq!(left(words, 31).0, [0x8000_0000, 0, 0x8000_0000, 0]);
        assert_eq!(right(words, 31).0, [0, 1, 1, 0]);
        assert_eq!(left(words, black_box(255)).0, [0; 4]);
        assert_eq!(right(words, black_box(255)).0, [0; 4]);
        assert_eq!(mask(black_box(Bytes([0xff; 16]))), 0xffff);
        assert_eq!(mask(black_box(Bytes([0; 16]))), 0);
    }
}
