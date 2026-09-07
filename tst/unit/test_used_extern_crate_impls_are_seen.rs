//@ compile-fail: type annotations needed
//@ aux-build: partial_eq_value_for_usize.rs
/* The counterpart of `test_unused_extern_crate_is_not_loaded`: once the crate is named,
   upstream loads it, and its `impl PartialEq<Value> for usize` makes `usize: PartialEq<?T>`
   ambiguous (E0283). */
fn meta() -> usize {
    8
}

fn main() {
    let _ = partial_eq_value_for_usize::Value;
    if let Some(expect) = None {
        assert_eq!(meta(), expect);
    }
}
