//@ compile-fail: Duplicate definition of #[panic_handler]
#[panic_handler]
fn first(_: &std::panic::PanicInfo<'_>) -> ! { loop {} }
#[panic_handler]
fn second(_: &std::panic::PanicInfo<'_>) -> ! { loop {} }
fn main() {}
