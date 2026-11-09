use std::marker::PhantomData;

trait Family {
    type Out<T>;
}

struct Identity;

impl Family for Identity {
    type Out<T> = T;
}

struct Receiver<S>(PhantomData<S>);

trait Wait<T> {
    fn wait(&self) -> Option<T>;
}

impl<S, T> Wait<T> for Receiver<S>
where
    S: Family<Out<T> = T>,
{
    fn wait(&self) -> Option<T> {
        None
    }
}

impl<S> Receiver<S> {
    fn install(&mut self, _source: S) {}
}

fn main() {
    let mut receiver = Receiver(PhantomData);
    let answer = receiver.wait();
    receiver.install(Identity);
    assert_eq!(answer, None::<u16>);
}
