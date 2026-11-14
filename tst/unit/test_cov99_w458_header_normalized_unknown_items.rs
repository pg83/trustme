//@ crate-type: lib
#![feature(inherent_associated_types)]

pub struct Pair<A, B>(A, B);

pub trait Split {
    type Left;
    type Right;
}

pub trait Project {
    type Out;
}

impl<T: Split> Project for T {
    type Out = Pair<T::Left, T::Right>;
}

impl<U> Pair<U, U> {
    pub type Diagonal = ();
}

impl<A, B> Pair<A, B> {
    pub type Output = u8;
}

pub struct Key;

impl Split for Key {
    type Left = u16;
    type Right = u32;
}

pub type Selected = <<Key as Project>::Out>::Output;
