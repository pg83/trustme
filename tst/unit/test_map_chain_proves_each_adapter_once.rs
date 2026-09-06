// A chain of `map`s: proving `Map<Map<..>, F>: Iterator` asks the impl's
// bounds `I: Iterator` and `F: FnMut(I::Item) -> B`, and each of those is a
// goal with associated-type bindings.  Upstream caches every canonical goal
// evaluation, bindings included, so each adapter is proven once.  Answering
// such goals afresh every time proves the whole chain beneath each adapter
// again, and the chain below takes minutes instead of a second (the unit
// runner gives a compile sixty seconds).

struct Iter<I>(I);

impl<I> Iterator for Iter<I>
where
    I: Iterator,
{
    type Item = I::Item;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

fn main() {
    let c = Iter(0i32..10)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .map(|x| x)
        .fold(0, |a, b| a + b);
    assert_eq!(c, 45);
}
