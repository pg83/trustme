//@ aux-build: foreign_glyph_aux.rs
// read-fonts' `IntSet::iter_from_u32` runs `it.and_then(|mut it| it.next())`
// on the value of `T::ordered_values_range(..)`, an `impl Iterator` in the
// trait `Domain`. The closure's parameter is that trait's opaque type; for
// `T = GlyphId` codegen sees `RangeInclusive<u32>`. rustc selects a closure's
// `Fn*` impl by the closure type alone (a builtin candidate); the argument
// tuple follows from its signature. Our closure impls carried the opaque
// projection in their header, which the revealed argument tuple did not
// match, and codegen found no `call_once`.
extern crate foreign_glyph_aux as aux;
use aux::Glyph;
use std::ops::RangeInclusive;

pub struct InDomain(u32);

trait Domain: Sized + Copy {
    fn to_u32(&self) -> u32;
    fn from_u32(member: InDomain) -> Self;
    fn ordered_values() -> impl DoubleEndedIterator<Item = u32>;
    fn ordered_values_range(range: RangeInclusive<Self>) -> impl DoubleEndedIterator<Item = u32>;
}

impl Domain for u32 {
    fn to_u32(&self) -> u32 {
        *self
    }
    fn from_u32(member: InDomain) -> Self {
        member.0
    }
    fn ordered_values() -> impl DoubleEndedIterator<Item = u32> {
        u32::MIN..=u32::MAX
    }
    fn ordered_values_range(range: RangeInclusive<u32>) -> impl DoubleEndedIterator<Item = u32> {
        range
    }
}

impl Domain for Glyph {
    fn to_u32(&self) -> u32 {
        Glyph::to_u32(*self)
    }
    fn from_u32(member: InDomain) -> Glyph {
        Glyph(member.0)
    }
    fn ordered_values() -> impl DoubleEndedIterator<Item = u32> {
        0u32..=9
    }
    fn ordered_values_range(range: RangeInclusive<Glyph>) -> impl DoubleEndedIterator<Item = u32> {
        range.start().to_u32()..=range.end().to_u32()
    }
}

struct Iter<SetIter, AllValuesIter> {
    set_values: SetIter,
    all_values: Option<AllValuesIter>,
}

impl<SetIter, AllValuesIter> Iter<SetIter, AllValuesIter>
where
    SetIter: Iterator<Item = u32>,
    AllValuesIter: Iterator<Item = u32>,
{
    fn new(set_values: SetIter, all_values: Option<AllValuesIter>) -> Self {
        Iter { set_values, all_values }
    }
}

impl<SetIter, AllValuesIter> Iterator for Iter<SetIter, AllValuesIter>
where
    SetIter: Iterator<Item = u32>,
    AllValuesIter: Iterator<Item = u32>,
{
    type Item = u32;
    fn next(&mut self) -> Option<u32> {
        let Some(all_values_it) = &mut self.all_values else {
            return self.set_values.next();
        };
        for index in all_values_it.by_ref() {
            if self.set_values.next() != Some(index) {
                return Some(index);
            }
        }
        None
    }
}

fn iter_from(s: &[u32], from: u32) -> std::iter::Skip<std::iter::Copied<std::slice::Iter<'_, u32>>> {
    let skip = s.iter().take_while(|v| **v < from).count();
    s.iter().copied().skip(skip)
}

enum Membership {
    Inclusive(Vec<u32>),
    Exclusive(Vec<u32>),
}

struct IntSet<T>(Membership, std::marker::PhantomData<T>);

impl<T: Domain> IntSet<T> {
    fn iter_from_u32(&self, value: T) -> impl Iterator<Item = u32> + '_ {
        match &self.0 {
            Membership::Inclusive(s) => Iter::new(iter_from(s, value.to_u32()), None),
            Membership::Exclusive(s) => {
                let value_u32 = value.to_u32();
                let max = T::ordered_values().next_back();
                let it = max.map(|max| T::ordered_values_range(value..=T::from_u32(InDomain(max))));
                let min = it.and_then(|mut it| it.next());
                if let (Some(min), Some(max)) = (min, max) {
                    Iter::new(
                        iter_from(s, value_u32),
                        Some(T::ordered_values_range(T::from_u32(InDomain(min))..=T::from_u32(InDomain(max)))),
                    )
                } else {
                    Iter::new(iter_from(s, value_u32), None)
                }
            }
        }
    }

    fn range_from(&self, start: T) -> impl Iterator<Item = T> + '_ {
        self.iter_from_u32(start).peekable().map(|v| T::from_u32(InDomain(v)))
    }
}

fn main() {
    let set: IntSet<Glyph> = IntSet(Membership::Exclusive(vec![4, 6]), std::marker::PhantomData);
    assert_eq!(set.range_from(Glyph(3)).collect::<Vec<_>>(), [Glyph(3), Glyph(4), Glyph(5), Glyph(6), Glyph(7), Glyph(8), Glyph(9)]);
    let set: IntSet<u32> = IntSet(Membership::Inclusive(vec![1, 4, 7]), std::marker::PhantomData);
    assert_eq!(set.range_from(3).collect::<Vec<_>>(), [4, 7]);
}
