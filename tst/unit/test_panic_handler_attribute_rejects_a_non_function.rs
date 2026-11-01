//@ compile-fail: #[panic_handler] on non-function
#[panic_handler]
struct Handler;
fn main() {}
