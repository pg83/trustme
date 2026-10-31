#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)] #[derive(Clone, Copy)] struct Values([f64; 2]);
extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.ucomieq.sd"] fn equal(left: Values, right: Values) -> i32;
    #[link_name = "llvm.x86.sse2.ucomige.sd"] fn greater_equal(left: Values, right: Values) -> i32;
    #[link_name = "llvm.x86.sse2.ucomigt.sd"] fn greater(left: Values, right: Values) -> i32;
    #[link_name = "llvm.x86.sse2.ucomile.sd"] fn less_equal(left: Values, right: Values) -> i32;
    #[link_name = "llvm.x86.sse2.ucomilt.sd"] fn less(left: Values, right: Values) -> i32;
    #[link_name = "llvm.x86.sse2.ucomineq.sd"] fn different(left: Values, right: Values) -> i32;
}
fn main() {
    unsafe {
        let left = Values([3.0, 101.0]); let right = Values([5.0, -103.0]);
        assert_eq!(equal(left, right), 0);
        assert_eq!(greater_equal(left, right), 0);
        assert_eq!(greater(left, right), 0);
        assert_eq!(less_equal(left, right), 1);
        assert_eq!(less(left, right), 1);
        assert_eq!(different(left, right), 1);
        assert_eq!(equal(left, left), 1);
        assert_eq!(greater(right, left), 1);
    }
}
