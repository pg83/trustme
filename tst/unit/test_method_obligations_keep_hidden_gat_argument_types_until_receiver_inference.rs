use std::marker::PhantomData;

trait Source {
    type Element;
}

trait Family {
    type Out<T>;
}

struct Seed;

impl Source for Seed {
    type Element = u8;
}

impl Family for Seed {
    type Out<T> = u16;
}

struct Receiver<S>(PhantomData<S>);

impl<S, U> Receiver<S>
where
    S: Source<Element = U> + Family<Out<U> = u16>,
{
    fn wait(&self) -> u16 {
        91
    }
}

impl<S> Receiver<S> {
    fn install(&mut self, _source: S) {}
}

fn main() {
    let mut receiver = Receiver(PhantomData);
    let answer = receiver.wait();
    receiver.install(Seed);
    assert_eq!(answer, 91);
}
