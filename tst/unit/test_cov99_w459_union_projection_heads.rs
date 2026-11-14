//@ crate-type: lib
//@ compile-fail: error:

use std::marker::PhantomData;
use std::ops::Deref;

pub trait Select { type Output: Select; }
pub trait Project<K> { type Output; }
pub struct Wrap<V>(PhantomData<V>);
impl<V> Project<u8> for Wrap<V> { type Output = (); }
impl<V> Project<u16> for Wrap<V> { type Output = (); }

pub struct Node<P, K, U>(PhantomData<(P, K)>, U);
impl<P, U: Select> Deref for Node<P, u8, Option<U>> {
    type Target = Node<U::Output, u16, U>;
    fn deref(&self) -> &Self::Target { unreachable!() }
}
impl<P, U: Select> Deref for Node<P, u16, Option<U>> {
    type Target = Node<U::Output, u8, U>;
    fn deref(&self) -> &Self::Target { unreachable!() }
}

fn heads<S, V, W, T: Select, U: Select>() -> (
    Node<(S, W, V), u8, Option<T::Output>>,
    Node<(u8, V, (<Wrap<W> as Project<S>>::Output, W)), u16, Option<U::Output>>,
) where Wrap<W>: Project<S> {
    (Node(PhantomData, None), Node(PhantomData, None))
}

pub fn exercise<T: Select, U: Select>(choose: bool) {
    let (a, b) = heads::<_, _, _, T, U>();
    let selected = if choose { &a } else { &b };
    let _ = selected;
}
