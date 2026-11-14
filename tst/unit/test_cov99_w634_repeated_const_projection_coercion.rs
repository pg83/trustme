//@ compile-only

struct Leaf<const N: usize>;

trait Element {
    type Output;
}

impl Element for Leaf<9> {
    type Output = u8;
}

fn pair<const N: usize>() -> (
    <Leaf<N> as Element>::Output,
    <Leaf<N> as Element>::Output,
)
where
    Leaf<N>: Element,
{
    loop {}
}

struct Sink;

impl Sink {
    fn accept(&self, _: &(u8, u8)) {}
}

fn main() {
    let mut values = pair();
    Sink.accept(&mut values);
}
