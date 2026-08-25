//@ check-pass

#[derive(Clone)]
struct Product<I, J>
where
    I: Iterator,
{
    left: I,
    right: J,
}

#[derive(Clone, Copy)]
enum SizeInfo {
    Sized(usize),
    Slice(usize, usize),
}

impl From<usize> for SizeInfo {
    fn from(size: usize) -> Self {
        Self::Sized(size)
    }
}

impl From<(usize, usize)> for SizeInfo {
    fn from((offset, element): (usize, usize)) -> Self {
        Self::Slice(offset, element)
    }
}

impl<I, J> Iterator for Product<I, J>
where
    I: Iterator,
    J: Clone + Iterator,
    I::Item: Clone,
{
    type Item = (I::Item, J::Item);

    fn next(&mut self) -> Option<Self::Item> {
        self.left.next().zip(self.right.next())
    }
}

fn product<I, J>(left: I, right: J) -> Product<I, J>
where
    I: Iterator,
    J: Clone + Iterator,
    I::Item: Clone,
{
    Product { left, right }
}

#[derive(Clone)]
struct ConsTuples<I, J>
where
    I: Iterator<Item = J>,
{
    iter: I,
}

impl<X, I, A, B> Iterator for ConsTuples<I, ((A, B), X)>
where
    I: Iterator<Item = ((A, B), X)>,
{
    type Item = (A, B, X);

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|((a, b), x)| (a, b, x))
    }
}

impl<X, I, A, B, C> Iterator for ConsTuples<I, ((A, B, C), X)>
where
    I: Iterator<Item = ((A, B, C), X)>,
{
    type Item = (A, B, C, X);

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|((a, b, c), x)| (a, b, c, x))
    }
}

impl<X, I, A, B, C, D> Iterator for ConsTuples<I, ((A, B, C, D), X)>
where
    I: Iterator<Item = ((A, B, C, D), X)>,
{
    type Item = (A, B, C, D, X);

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|((a, b, c, d), x)| (a, b, c, d, x))
    }
}

fn cons_tuples<I, J>(iterable: I) -> ConsTuples<I::IntoIter, J>
where
    I: IntoIterator<Item = J>,
{
    ConsTuples { iter: iterable.into_iter() }
}

fn main() {
    let sizes = (0..2)
        .map(Into::<SizeInfo>::into)
        .chain(product(0..2, 0..2).map(Into::<SizeInfo>::into));
    let pairs = product(sizes, [1usize, 2].into_iter());
    let triples = cons_tuples(product(pairs, [0usize].into_iter()));
    let quads = cons_tuples(product(triples, [0usize].into_iter()));
    let quints = cons_tuples(product(quads, [false, true].into_iter()));
    quints.for_each(|_| {});
}
