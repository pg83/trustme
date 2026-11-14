//@ crate-type: lib
#![feature(coerce_unsized)]

use std::marker::PhantomData;
use std::ops::CoerceUnsized;

pub struct Shell<P, Tag>(pub P, pub PhantomData<Tag>);

impl<P, Q, Tag> CoerceUnsized<Shell<Q, Tag>> for Shell<P, Tag>
where
    P: CoerceUnsized<Q>,
{}

pub fn change_marker<P, From, To>(value: Shell<P, From>) -> Shell<P, To>
where
    Shell<P, From>: CoerceUnsized<Shell<P, To>>,
{
    value
}
