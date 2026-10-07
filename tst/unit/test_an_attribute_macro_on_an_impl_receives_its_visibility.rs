//@ proc-macro-aux-build: item_head_text.rs
// fs-mistrust and tor-basic-utils (arti) write `#[ext] pub impl Path { .. }`;
// the `extend` attribute turns it into `pub trait PathExt` and gives the trait
// the impl's visibility. Upstream parses a visibility on any item and rejects
// it on an impl only after expansion (ast_validation), so the attribute gets
// `pub impl Path`. We dropped the `pub`, the trait came out private, and
// `use fs_mistrust::anon_home::PathExt` found nothing.
use item_head_text::item_head;

#[item_head]
pub impl Path {
    fn f() {}
}

fn main() {
    assert_eq!(ITEM_HEAD, "pub impl Path");
}
