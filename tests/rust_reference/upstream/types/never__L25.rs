// Extracted from src/types/never.md:25
#![allow(unused)]
fn main() {
    unsafe extern "C" {
        pub safe fn no_return_extern_func() -> !;
    }
}
