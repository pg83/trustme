// Extracted from src/attributes/diagnostics.md:338
#![allow(unused)]
fn main() {
    #[deprecated(since = "5.2.0", note = "foo was rarely used. Users should instead use bar")]
    pub fn foo() {}
    
    pub fn bar() {}
}
