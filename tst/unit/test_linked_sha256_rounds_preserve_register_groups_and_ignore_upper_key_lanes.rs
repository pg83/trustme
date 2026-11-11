#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)]
#[derive(Clone, Copy)]
struct Group([u32; 4]);

extern "unadjusted" {
    #[link_name = "llvm.x86.sha256rnds2"]
    fn rounds(cdgh: Group, abef: Group, keys: Group) -> Group;
}

fn main() {
    use std::hint::black_box;

    let zero = black_box(Group([0; 4]));
    unsafe {
        let first = rounds(zero, zero, black_box(Group([1, 0, 0xdead_beef, 0x1234_5678])));
        assert_eq!(first.0, [1, 0x0420_0080, 1, 0x4428_0480]);
        let second = rounds(zero, zero, black_box(Group([0, 1, 0xffff_ffff, 0x8000_0000])));
        assert_eq!(second.0, [0, 1, 0, 1]);
        let from_d = rounds(black_box(Group([0, 0, 1, 0])), zero, zero);
        assert_eq!(from_d.0, [1, 0x0420_0080, 0, 0x0420_0080]);
        let wrapped = rounds(zero, zero, black_box(Group([u32::MAX, 0, 17, 19])));
        assert_eq!(wrapped.0, [u32::MAX, u32::MAX, u32::MAX, u32::MAX - 1]);
    }
}
