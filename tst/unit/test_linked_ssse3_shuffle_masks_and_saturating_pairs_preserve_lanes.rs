#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)] #[derive(Clone, Copy)] struct Bytes([u8; 16]);
#[repr(simd)] #[derive(Clone, Copy)] struct SignedBytes([i8; 16]);
#[repr(simd)] #[derive(Clone, Copy)] struct Words([i16; 8]);
extern "unadjusted" {
    #[link_name = "llvm.x86.ssse3.pshuf.b.128"]
    fn shuffle(value: Bytes, mask: Bytes) -> Bytes;
    #[link_name = "llvm.x86.ssse3.pmadd.ub.sw.128"]
    fn pairs(left: Bytes, right: SignedBytes) -> Words;
}
#[target_feature(enable = "ssse3")]
unsafe fn check() {
    let input = Bytes([10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25]);
    let mask = Bytes([15,14,13,12,11,10,9,8,0x80,0x1f,0x70,3,0xff,0,0x85,7]);
    let result: [u8; 16] = core::mem::transmute(shuffle(input, mask));
    assert_eq!(result, [25,24,23,22,21,20,19,18,0,25,10,13,0,10,0,17]);
    let left = Bytes([255,255,255,255,1,2,3,4,10,20,30,40,250,250,0,0]);
    let right = SignedBytes([127,127,-128,-128,1,-1,2,-2,3,4,-3,-4,100,100,127,-128]);
    let result: [i16; 8] = core::mem::transmute(pairs(left, right));
    assert_eq!(result, [32767,-32768,-1,-2,110,-250,32767,0]);
}
fn main() {
    if is_x86_feature_detected!("ssse3") { unsafe { check(); } }
}
