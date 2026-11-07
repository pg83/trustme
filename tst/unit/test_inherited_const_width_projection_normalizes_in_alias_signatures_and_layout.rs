#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Tags {
    type Kind;
}

struct Scalar;
impl Tags for Scalar {
    type Kind = u16;
}

trait Parent<Kind, const WIDTH: usize> {
    type Bytes;
}

trait Child<Tag: Tags, const BASE: usize>: Parent<Tag::Kind, { BASE + 1 }>
where
    [(); BASE + 1]:,
{}

struct Backing;
impl Parent<u16, 4> for Backing {
    type Bytes = [u8; 4];
}
impl Child<Scalar, 3> for Backing {}

trait Producer<const BASE: usize>
where
    [(); BASE + 1]:,
{
    type Tag: Tags<Kind = u16>;
    type Data: Child<Self::Tag, BASE, Bytes = [u8; BASE + 1]>;
}

struct Concrete;
impl Producer<3> for Concrete {
    type Tag = Scalar;
    type Data = Backing;
}

type Bytes<P, const BASE: usize> =
    <<P as Producer<BASE>>::Data as Parent<u16, { BASE + 1 }>>::Bytes;

fn copied<P: Producer<BASE>, const BASE: usize>(input: &Bytes<P, BASE>) -> Bytes<P, BASE>
where
    [(); BASE + 1]:,
{
    *input
}

const WIDTH: usize = std::mem::size_of::<Bytes<Concrete, 3>>();

fn main() {
    assert_eq!(WIDTH, 4);
    let source = [31u8, 37, 41, 43];
    let result = copied::<Concrete, 3>(&source);
    assert_eq!(result, source);
    assert_eq!(std::mem::size_of_val(&result), 4);
}
