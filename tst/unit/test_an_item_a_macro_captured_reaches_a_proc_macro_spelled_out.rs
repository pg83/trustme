// `macro_rules!` matches `$test:item` as a single token, but a proc macro is handed
// the item spelled out: upstream transcribes the capture as `TokenStream::from_ast(item)`
// - the item's attributes followed by its tokens - wrapped in an invisible
// `MetaVar(Item)` delimiter (`transcribe_metavar`, rustc_expand/src/mbe/transcribe.rs),
// and the proc-macro server maps that invisible delimiter to `Delimiter::None`
// (`Delimiter::from_internal`, rustc_expand/src/proc_macro_server.rs), so the item lands
// directly in the macro's input. We sent the fragment as one opaque token and aborted on
// it. rstest_reuse's `#[apply(..)]` passes the annotated function to `merge_attrs!` this
// way, which is how base64's engine tests reach it.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

macro_rules! apply_template {
    ($test:item) => {
        proc_macro_item_passthrough::echo! { $test }
    };
}

apply_template! {
    #[inline]
    pub fn from_a_captured_item() -> u32 {
        7
    }
}

fn main() {
    assert_eq!(from_a_captured_item(), 7);
}
