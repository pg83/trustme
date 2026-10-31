//@ compile-fail: #[panic_handler] on non-function
#[panic_handler]
struct Wrong;
fn main() {}
