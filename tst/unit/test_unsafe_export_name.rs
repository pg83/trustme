// `#[unsafe(export_name = "...")]` gives an item its exported symbol name. Only
// `no_mangle`, `link_section`, `ffi_const` and `naked` were known inside the
// `unsafe(...)` wrapper, so this one was a hard error.
//
// Same shape as the upstream test attributes/unsafe/unsafe-attributes.rs.

#[unsafe(export_name = "trustme_test_exported_fn")]
pub fn renamed() -> u32 {
    7
}

// `#[unsafe(no_mangle)]` next to it is a lint in rustc, not an error, and the
// exported name is the one given here.
#[unsafe(no_mangle)]
#[unsafe(export_name = "trustme_test_exported_both")]
pub fn renamedTwice() -> u32 {
    9
}

#[unsafe(export_name = "trustme_test_exported_static")]
pub static RENAMED: u32 = 11;

fn main() {
    assert_eq!(renamed(), 7);
    assert_eq!(renamedTwice(), 9);
    assert_eq!(RENAMED, 11);
}
