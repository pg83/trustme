// Extracted from library/core/src/str/mod.rs:2468
#![allow(unused)]
#![feature(trim_prefix_suffix)]
fn main() {
    
    // Prefix present - removes it
    assert_eq!("foo:bar".trim_prefix("foo:"), "bar");
    assert_eq!("foofoo".trim_prefix("foo"), "foo");
    
    // Prefix absent - returns original string
    assert_eq!("foo:bar".trim_prefix("bar"), "foo:bar");
    
    // Method chaining example
    assert_eq!("<https://example.com/>".trim_prefix('<').trim_suffix('>'), "https://example.com/");
}
