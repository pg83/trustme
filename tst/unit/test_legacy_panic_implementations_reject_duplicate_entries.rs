//@ compile-fail: Duplicate definition of #[panic_implementation]
#[panic_implementation]
fn first(_: &std::panic::PanicInfo<'_>) -> ! { loop {} }
#[panic_implementation]
fn second(_: &std::panic::PanicInfo<'_>) -> ! { loop {} }
fn main() {}
