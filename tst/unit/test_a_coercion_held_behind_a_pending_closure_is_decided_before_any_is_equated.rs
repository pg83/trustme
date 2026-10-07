// num-bigint-dig 0.8.6 from_radix_digits_be: `data` is a SmallVec whose array
// only `Digits(data)` at the end fixes, `add2(&mut data, ..)` needs its deref to
// `&mut [u64]`, and both coercions wait behind the closures of the folds. When
// nothing else moves, every coercion that can be decided is decided - each a
// change, so no numeric default runs in the same round - before one is
// equated outright: the constructor's fixes the array, the deref then goes
// through, and the folds' `0` is u64, not a defaulted i32.
use std::ops::{Deref, DerefMut};

pub trait Array {
    type Item;
}

macro_rules! impl_array {
    ($($size:expr),+) => {
        $(
            impl<T> Array for [T; $size] {
                type Item = T;
            }
        )+
    };
}

impl_array!(0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 32);

pub struct SmallVec<A: Array> {
    items: Vec<A::Item>,
}

impl<A: Array> SmallVec<A> {
    pub fn with_capacity(n: usize) -> Self {
        SmallVec { items: Vec::with_capacity(n) }
    }
    pub fn push(&mut self, item: A::Item) {
        self.items.push(item);
    }
}

impl<A: Array> Deref for SmallVec<A> {
    type Target = [A::Item];
    fn deref(&self) -> &[A::Item] {
        &self.items
    }
}

impl<A: Array> DerefMut for SmallVec<A> {
    fn deref_mut(&mut self) -> &mut [A::Item] {
        &mut self.items
    }
}

fn add2(a: &mut [u64], b: &[u64]) {
    a[0] += b[0];
}

const VEC_SIZE: usize = 4;
type BigDigit = u64;

fn finish(data: SmallVec<[BigDigit; VEC_SIZE]>) -> u64 {
    data[0]
}


pub struct Digits(SmallVec<[u64; VEC_SIZE]>);

fn from_digits(v: &[u8], radix: u32) -> Digits {
    let radix = radix as BigDigit;
    let mut data = SmallVec::with_capacity(4);
    let (head, tail) = v.split_at(1);
    let first = head.iter().fold(0, |acc, &d| acc * radix + d as BigDigit);
    data.push(first);
    for chunk in tail.chunks(1) {
        if data.last() != Some(&0) {
            data.push(0);
        }
        let n = chunk.iter().fold(0, |acc, &d| acc * radix + d as BigDigit);
        add2(&mut data, &[n]);
    }
    Digits(data)
}

fn main() {
    let digits = from_digits(&[1, 2], 10);
    assert_eq!(finish(digits.0), 3);
}
