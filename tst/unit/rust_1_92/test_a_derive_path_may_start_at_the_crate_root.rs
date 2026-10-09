// A path in `#[derive(..)]` is an ordinary path (rustc resolves each with
// `resolve_derives` after parsing it as a path), so it may start with `crate`,
// `self` or `super`. rkyv derives its own types with `#[rkyv(crate)]`, and
// its `Archive` derive emits `#[derive(crate::bytecheck::CheckBytes)]`.
mod derives {
    pub use core::clone::Clone;
    pub use core::fmt::Debug;
}

#[derive(crate::derives::Debug, self::derives::Clone)]
struct Point {
    x: i32,
}

mod inner {
    #[derive(super::derives::Debug)]
    pub struct Inner(pub u8);
}

fn main() {
    let point = Point { x: 3 };
    assert_eq!(format!("{:?}", point.clone()), "Point { x: 3 }");
    assert_eq!(format!("{:?}", inner::Inner(7)), "Inner(7)");
}
