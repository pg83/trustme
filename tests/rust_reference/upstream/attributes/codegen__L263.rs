// Extracted from src/attributes/codegen.md:263
#![allow(unused)]
fn main() {
    #[cfg(target_feature = "sse2")] {
    #[target_feature(enable = "sse")]
    fn foo_sse() {}
    
    fn bar() {
        // Calling `foo_sse` here is unsafe, as we must ensure that SSE is
        // available first, even if `sse` is enabled by default on the target
        // platform or manually enabled as compiler flags.
        unsafe {
            foo_sse();
        }
    }
    
    #[target_feature(enable = "sse")]
    fn bar_sse() {
        // Calling `foo_sse` here is safe.
        foo_sse();
        || foo_sse();
    }
    
    #[target_feature(enable = "sse2")]
    fn bar_sse2() {
        // Calling `foo_sse` here is safe because `sse2` implies `sse`.
        foo_sse();
    }
    }
}
