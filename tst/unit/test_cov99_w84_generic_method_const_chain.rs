#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::ops::Deref;

trait Select { type Output; }
impl Select for u8 { type Output = u16; }
impl Select for u16 { type Output = u8; }

const fn width<T>() -> usize { std::mem::size_of::<T>() }

struct Chain<T, U>(Option<T>, U);
impl<T, U> Deref for Chain<T, U> {
    type Target = U;
    fn deref(&self) -> &U { &self.1 }
}

struct Factory;
impl Factory {
    fn chain<U: Select, const N: usize>(&self) -> Chain<U, Chain<(U::Output, U, [u8; N]), ()>> {
        Chain(None, Chain(None, ()))
    }
}

fn exercise<T>() where [(); width::<T>()]: {
    let outer = Factory.chain::<_, { width::<T>() }>();
    let selected = if std::hint::black_box(true) { &outer } else { &outer.1 };
    let output: Option<(_, u8, _)> = selected.0;
    assert!(output.is_none());
}

fn main() { exercise::<u32>(); }
