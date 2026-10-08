//@ proc-macro-aux-build: item_text.rs
// pin-project's tests put `#[cfg_attr(not(any()), pin)]` on a field of a
// `#[pin_project]` struct. rustc configures only the item's own attributes
// before it calls an attribute macro: a `cfg_attr` on the item is expanded,
// one on a field is handed over as written (only a derive's input is
// configured throughout). We sent a nested `cfg_attr` both expanded and as
// written; pin-project's derive then expanded the written one too and
// rejected the field's second `#[pin]`.
use item_text::item_text;

#[item_text]
#[cfg_attr(all(), derive(Clone))]
#[cfg_attr(any(), derive(Debug))]
struct S {
    #[cfg_attr(all(), allow(unused))]
    #[cfg_attr(any(), deny(unused))]
    x: u8,
}

fn main() {
    let text: String = ITEM.split_whitespace().collect();
    assert!(text.starts_with("#[derive(Clone)]structS"), "{}", ITEM);
    assert!(text.contains("{#[cfg_attr(all(),allow(unused))]#[cfg_attr(any(),deny(unused))]x:u8"), "{}", ITEM);
    assert!(!text.contains("Debug"), "{}", ITEM);
    assert!(!text.contains("#[allow(unused)]"), "{}", ITEM);
}
