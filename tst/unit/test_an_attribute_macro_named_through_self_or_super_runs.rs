//@ proc-macro-aux-build: item_head_text.rs
// time-macros' test of the use tree was written `#[super::attr]` at first, and
// the attribute never ran: an attribute path starting with `self` or `super`
// was looked up as a module of that name, found nothing, and the attribute was
// dropped as if it were inert. Upstream resolves the path like any other, from
// the current module or its parent.
extern crate item_head_text;
#[allow(unused_imports)]
use item_head_text::item_head;

pub mod parent {
    #[super::item_head]
    pub fn from_the_parent() {}
}

pub mod own {
    use item_head_text::item_head;

    #[self::item_head]
    pub struct FromItself;
}

fn main() {
    assert_eq!(parent::ITEM_HEAD, "pub fn from_the_parent");
    assert_eq!(own::ITEM_HEAD, "pub struct FromItself");
}
