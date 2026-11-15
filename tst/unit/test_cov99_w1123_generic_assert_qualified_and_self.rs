#![feature(generic_assert)]

const ENABLED: bool = true;

#[derive(Debug)]
struct Marker;

impl Marker {
    fn same(&self, other: &Self) {
        assert!(core::ptr::eq(self, other) && crate::ENABLED);
    }
}

fn main() {
    let marker = Marker;
    marker.same(&marker);
}
