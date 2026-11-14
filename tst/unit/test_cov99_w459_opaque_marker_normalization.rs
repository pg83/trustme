//@ crate-type: lib
//@ compile-fail: error:

use std::marker::PhantomData;
use std::ops::Deref;

pub trait Select { type Output: Select; }
pub trait Project<K> { type Output; }
pub struct Wrap<V>(PhantomData<V>);
impl<V> Project<u8> for Wrap<V> { type Output = (); }
impl<V> Project<u16> for Wrap<V> { type Output = (V,); }

type Left<S, V, W> = (S, W, V);
type Right<S, V, W> where Wrap<W>: Project<S> =
    (u8, V, (<Wrap<W> as Project<S>>::Output, W));

pub trait Route<P> { type Next; }
struct Forward<S, V, W>(PhantomData<(S, V, W)>);
struct Backward<S, V, W>(PhantomData<(S, V, W)>);
impl<S, V, W> Route<Left<S, V, W>> for Forward<S, V, W>
where Wrap<W>: Project<S> {
    type Next = Right<S, V, W>;
}
impl<S, V, W> Route<Right<S, V, W>> for Backward<S, V, W>
where Wrap<W>: Project<S> {
    type Next = Left<S, V, W>;
}

fn forward<S, V, W>(_: &PhantomData<Left<S, V, W>>)
    -> impl Route<Left<S, V, W>, Next = Right<S, V, W>>
where Wrap<W>: Project<S> { Forward::<S, V, W>(PhantomData) }

fn backward<S, V, W>(_: &PhantomData<Left<S, V, W>>)
    -> impl Route<Right<S, V, W>, Next = Left<S, V, W>>
where Wrap<W>: Project<S> { Backward::<S, V, W>(PhantomData) }

pub struct Node<P, M, U>(PhantomData<P>, M, U);
impl<P, M: Route<P>, U: Select> Deref for Node<P, M, Option<U>> {
    type Target = Node<M::Next, M, U>;
    fn deref(&self) -> &Self::Target { unreachable!() }
}

fn data<S, V, W>() -> (PhantomData<Left<S, V, W>>, PhantomData<Right<S, V, W>>)
where Wrap<W>: Project<S> { (PhantomData, PhantomData) }

pub fn exercise<T: Select, U: Select>(choose: bool) {
    let (left, right) = data();
    let a = Node(left, forward(&left), None::<T::Output>);
    let b = Node(right, backward(&left), None::<U::Output>);
    let selected = if choose { &a } else { &b };
    let _ = selected;
}
