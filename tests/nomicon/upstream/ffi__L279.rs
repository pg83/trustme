// Extracted from src/ffi.md:279
#[unsafe(no_mangle)]
pub extern "C" fn hello_from_rust() {
    println!("Hello from Rust!");
}
fn main() {}
