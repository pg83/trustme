//@ test-harness
//@ proc-macro-aux-build: strips_test_attributes.rs
// rustc expands `#[test]` before the attributes after it and builds the test's
// descriptor then, from the item's attributes as they are at that point
// (`expand_test_or_bench`, rustc_builtin_macros/src/test.rs: `should_panic`
// and `should_ignore` read `item.attrs`). `#[should_panic]` and `#[ignore]`
// are inert. wasm-bindgen-test's attribute, written after `#[test]`, takes
// both out of the function it re-emits, and jsonwebtoken's
// `decode_token_missing_parts` still expects its panic.
use strips_test_attributes::strips_test_attributes;

#[test]
#[strips_test_attributes]
#[should_panic(expected = "invalid token")]
fn panics_as_expected() {
    panic!("an invalid token");
}

#[test]
#[strips_test_attributes]
#[ignore]
fn ignored() {
    panic!("an ignored test runs only when asked");
}
