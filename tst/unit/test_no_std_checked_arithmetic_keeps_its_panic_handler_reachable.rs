//@ compile-flags: -Cpanic=abort

#![no_std]
#![no_main]

extern "C" {
    fn exit(status: i32) -> !;
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { exit(0) }
}

fn checked(value: u16) -> u16 {
    value + 1
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    if checked(core::hint::black_box(12)) != 13 {
        return 1;
    }
    let _ = checked(core::hint::black_box(u16::MAX));
    2
}
