// Extracted from src/attributes/codegen.md:233
#![allow(unused)]
fn main() {
    #[cfg(target_feature = "avx2")]
    #[target_feature(enable = "avx2")]
    fn foo_avx2() {}
}
