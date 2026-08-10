// Extracted from src/items/external-blocks.md:436
#![allow(unused)]
fn main() {
    #[cfg(all(windows, target_arch = "x86"))]
    #[link(name = "exporter", kind = "raw-dylib")]
    unsafe extern "stdcall" {
        #[link_ordinal(15)]
        safe fn imported_function_stdcall(i: i32);
    }
}
