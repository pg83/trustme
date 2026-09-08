// A method candidate that is `#[unstable]` in another crate, its feature not enabled here,
// is set aside while a stable candidate applies (rustc `consider_candidates`, the
// `unstable_name_collisions` lint's case): `Panicking.intersperse(0)` with the crate's
// `Itertools` in scope is not ambiguous with core's unstable `Iterator::intersperse`.
// (itertools `tests/laziness.rs:61`)
pub struct Intersperse<I: Iterator> {
    iter: I,
    separator: I::Item,
    pending: Option<I::Item>,
    started: bool,
}

impl<I: Iterator> Iterator for Intersperse<I>
where
    I::Item: Clone,
{
    type Item = I::Item;
    fn next(&mut self) -> Option<I::Item> {
        if let Some(item) = self.pending.take() {
            return Some(item);
        }
        let next = self.iter.next()?;
        if self.started {
            self.pending = Some(next);
            Some(self.separator.clone())
        } else {
            self.started = true;
            Some(next)
        }
    }
}

pub trait Itertools: Iterator {
    fn intersperse(self, element: Self::Item) -> Intersperse<Self>
    where
        Self: Sized,
        Self::Item: Clone,
    {
        Intersperse { iter: self, separator: element, pending: None, started: false }
    }
}

impl<T: Iterator + ?Sized> Itertools for T {}

struct Panicking;

impl Iterator for Panicking {
    type Item = u8;
    fn next(&mut self) -> Option<u8> {
        panic!("iterator adaptor is not lazy")
    }
}

fn main() {
    let _ = Panicking.intersperse(0);
    let v: Vec<u8> = vec![1, 2, 3].into_iter().intersperse(9).collect();
    assert_eq!(v, [1, 9, 2, 9, 3]);
}
