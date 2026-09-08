/* An impl whose self type is written with a projection of one of its parameters
   (`impl<I: Iterator> Has<I> for (I::Item,)`, itertools' `HasCombination`) matches a
   concrete goal only through that projection: the structural match cannot decide
   `&u8 = <Iter<u8> as Iterator>::Item` by itself and leaves it pending.  Upstream
   equates the impl's trait reference and keeps what is left as goals of the candidate,
   whose certainty is what those goals answer, so normalizing the projection proves the
   candidate - and `Comb<Iter<u8>, (&u8,)>` is an `Iterator` when its methods are
   translated. */

trait Has<I> {
    type C: Iterator<Item = Self>;
}

struct One<I> {
    iter: I,
}

impl<I: Iterator> Iterator for One<I> {
    type Item = (I::Item,);

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|x| (x,))
    }
}

impl<I: Iterator> Has<I> for (I::Item,) {
    type C = One<I>;
}

struct Comb<I, T: Has<I>> {
    inner: T::C,
}

impl<I: Iterator, T: Has<I>> Iterator for Comb<I, T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        self.inner.next()
    }
}

fn main() {
    let v = [1u8, 2, 3];
    let mut c: Comb<_, (&u8,)> = Comb {
        inner: One { iter: v.iter() },
    };
    assert_eq!(c.nth(1), Some((&2u8,)));
    let c2: Comb<_, (&u8,)> = Comb {
        inner: One { iter: v.iter() },
    };
    assert_eq!(c2.last(), Some((&3u8,)));
}
