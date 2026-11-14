//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(sized_hierarchy)]

fn opaque() -> &'static (impl ?Sized + core::fmt::Debug) {
    "w407"
}

fn needs_metadata<T: ?Sized + core::marker::MetaSized>(_: &T) {}

fn main() {
    needs_metadata(opaque());
}
