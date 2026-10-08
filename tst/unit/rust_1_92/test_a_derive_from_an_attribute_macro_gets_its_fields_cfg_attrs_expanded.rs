//@ proc-macro-aux-build: item_text.rs
// `#[pin_project]` hands its struct on with its fields' attributes as
// written and puts a derive on it. rustc configures a derive's input
// throughout (`cfg_eval`), so `#[cfg_attr(not(any()), pin)]` on a field
// reaches pin-project's derive as `#[pin]`. Ours did not, the field was
// taken as unpinned, and `Foo<PhantomPinned>` was `Unpin`.
use item_text::with_input_text;

#[with_input_text]
struct S {
    #[cfg_attr(all(), note)]
    x: u8,
    #[cfg_attr(any(), note)]
    y: u8,
}

fn main() {
    let text: String = DERIVE_INPUT.split_whitespace().collect();
    assert_eq!(text, "structS{#[note]x:u8,y:u8,}", "{}", DERIVE_INPUT);
}
