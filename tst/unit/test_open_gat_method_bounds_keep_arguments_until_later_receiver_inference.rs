use std::marker::PhantomData;

trait Family {
    type Out<T>;
}

struct Identity;

impl Family for Identity {
    type Out<T> = T;
}

struct Receiver<S>(PhantomData<S>);

impl<S> Receiver<S> {
    fn wait<T>(&self) -> Option<T>
    where
        S: Family<Out<T> = T>,
    {
        None
    }

    fn install(&mut self, _source: S) {}
}

fn main() {
    let mut receiver = Receiver(PhantomData);
    let answer = receiver.wait();
    receiver.install(Identity);
    assert_eq!(answer, None::<u16>);
}
