//@ proc-macro-aux-build: item_replacer.rs
// ptr_meta's tests write `#[crate::pointee(crate)]` for the attribute macro
// the crate re-exports with `pub use ptr_meta_derive::pointee;`. rustc
// resolves an attribute's path like any other path, and `crate::` starts at
// the crate root. We looked the path up as a relative one whose first
// segment is a module named `crate`, found nothing, and left the attribute
// on the item unexpanded: the trait got no `Pointee` impl.
pub use item_replacer::make_seven;

#[crate::make_seven]
struct Placeholder;

fn main() {
    #[crate::make_seven]
    struct InBody;

    assert_eq!(seven(), 7);
}
