// Extracted from library/core/src/str/mod.rs:2505
#![allow(unused)]
#![feature(trim_prefix_suffix)]
fn main() {

    // Suffix present - removes it
    assert_eq!("bar:foo".trim_suffix(":foo"), "bar");
    assert_eq!("foofoo".trim_suffix("foo"), "foo");

    // Suffix absent - returns original string
    assert_eq!("bar:foo".trim_suffix("bar"), "bar:foo");

    // Method chaining example
    assert_eq!("<https://example.com/>".trim_prefix('<').trim_suffix('>'), "https://example.com/");
}
