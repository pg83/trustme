//@ compile-only
//@ compile-flags: -C emit-cpp-only -C panic=abort
//@ output-contains: .cpp u32 panic_impl(uintptr_t payload) {extern u32
//@ output-contains: .cpp ;return
#![no_std]
#![no_main]

#[cfg_attr(rust_compiler = "trustme", panic_implementation)]
fn returning_panic(payload: usize) -> u32 {
    payload as u32
}

#[cfg(not(rust_compiler = "trustme"))]
#[panic_handler]
fn native_panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}
