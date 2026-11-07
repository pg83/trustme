#![feature(generic_const_exprs, generic_arg_infer)]
#![allow(incomplete_features)]

trait Dimensions {
    type Axis;
}

struct Axes;
impl Dimensions for Axes {
    type Axis = u16;
}

trait Row<Axis, const WIDTH: usize> {
    type Output;
    fn row() -> Self::Output;
}

trait Grid<D: Dimensions, const BASE: usize>: Row<D::Axis, { BASE + 1 }>
where
    [(); BASE + 1]:,
{}

struct Storage;
impl Row<u16, 4> for Storage {
    type Output = [u8; 4];
    fn row() -> [u8; 4] { [17, 19, 23, 29] }
}
impl Grid<Axes, 3> for Storage {}

trait Source<const BASE: usize>
where
    [(); BASE + 1]:,
{
    type Dimensions: Dimensions<Axis = u16>;
    type Store: Grid<Self::Dimensions, BASE, Output = [u8; BASE + 1]>;
}

struct Schema;
impl Source<3> for Schema {
    type Dimensions = Axes;
    type Store = Storage;
}

fn take<Store, Axis, const WIDTH: usize>() -> <Store as Row<Axis, WIDTH>>::Output
where
    Store: Row<Axis, WIDTH>,
{
    Store::row()
}

fn read<S: Source<BASE>, const BASE: usize>() -> [u8; BASE + 1]
where
    [(); BASE + 1]:,
{
    take::<S::Store, _, _>()
}

fn main() {
    assert_eq!(read::<Schema, 3>(), [17, 19, 23, 29]);
    assert_eq!(take::<Storage, u16, 4>(), [17, 19, 23, 29]);
}
