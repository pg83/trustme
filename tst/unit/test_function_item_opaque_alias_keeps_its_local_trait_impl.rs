//@ compile-flags: -Cpanic=abort

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

type Callable = impl Fn() -> u16;

fn source() -> u16 {
    613
}

#[define_opaque(Callable)]
fn make() -> Callable {
    source
}

trait Label {
    fn label(&self) -> u16;
}

impl Label for Callable {
    fn label(&self) -> u16 {
        617
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    let callable = make();
    if callable() == 613 && callable.label() == 617 { 0 } else { 1 }
}
