// Extracted from src/expressions/block-expr.md:344
#![allow(unused)]
fn main() {
    fn is_unix_platform() -> bool {
        #[cfg(unix)] { true }
        #[cfg(not(unix))] { false }
    }
}
