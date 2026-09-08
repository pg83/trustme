//@ crate-type: lib
// plotters: an impl whose trait arguments name `X::ValueType` and whose method
// declares an item.  The items declared in a method are named after the method,
// by its path; that path was copied at lowering, before `X::ValueType` was
// resolved to `<X as Ranged>::ValueType`, and the rlib writer met the
// unresolved projection.
pub trait Ranged {
    type ValueType;
}

pub trait PointCollection<Coord> {
    fn describe(&self) -> usize;
}

pub struct Series<X>(pub X);

impl<X: Ranged> PointCollection<(X::ValueType, u32)> for Series<X> {
    fn describe(&self) -> usize {
        struct Local(usize);
        let local = Local(1);
        local.0
    }
}
