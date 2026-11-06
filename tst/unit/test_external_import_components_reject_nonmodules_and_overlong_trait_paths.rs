//@ aux-build: external_import_component_kinds.rs
//@ reject-flags: --edition=2021 --cfg missing => error || error
//@ reject-flags: --edition=2021 --cfg nonmodule => error || error
//@ reject-flags: --edition=2021 --cfg direct_trait => error || error
//@ reject-flags: --edition=2021 --cfg alias_trait => error || error
//@ reject-flags: --edition=2021 --cfg variant => error || error
#![feature(decl_macro, import_trait_associated_functions)]
extern crate external_import_component_kinds as source;
#[cfg(missing)] use source::hub::leaf::absent::Item;
#[cfg(nonmodule)] use source::hub::Marker::Child;
#[cfg(direct_trait)] use source::hub::Shape::Item::Leaf;
#[cfg(alias_trait)] use source::hub::AliasShape::Item::Leaf;
#[cfg(variant)] use source::hub::Choice::First::Item;
fn main() {
    assert_eq!(source::check_first!(), 17);
    assert_eq!(source::hub::leaf::VALUE, 31);
}
