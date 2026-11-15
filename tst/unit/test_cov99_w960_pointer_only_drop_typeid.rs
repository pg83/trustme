//@ compile-only
//@ compile-flags: -C codegen-type=monomir -Z mir-opt-level=0
#![feature(core_intrinsics)]

struct Token;

impl Drop for Token {
    #[inline(never)]
    fn drop(&mut self) {
        core::hint::black_box(unsafe { core::intrinsics::type_id::<[u16; 3]>() });
    }
}

fn main() {
    unsafe { core::ptr::drop_in_place(1usize as *mut Token) };
}
