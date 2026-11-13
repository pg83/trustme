trait Entry {
    type Value;
    fn read(self) -> Self::Value;
}

struct Source(u16);

impl Entry for Source {
    type Value = u16;
    fn read(self) -> u16 {
        self.0
    }
}

struct Wrapped<T>(T);

impl<T: Entry> Entry for Wrapped<T>
where
    T::Value: Clone,
{
    type Value = T::Value;
    fn read(self) -> Self::Value {
        self.0.read().clone()
    }
}

trait Mapper<T> {
    type Output;
    fn apply(self, value: T) -> Self::Output;
}

struct Identity;

impl<T: Entry> Mapper<T> for Identity {
    type Output = T::Value;
    fn apply(self, value: T) -> Self::Output {
        value.read()
    }
}

struct Apply<F, T>(F, T);

impl<F: Mapper<T>, T> Entry for Apply<F, T> {
    type Value = <F as Mapper<T>>::Output;
    fn read(self) -> Self::Value {
        self.0.apply(self.1)
    }
}

fn extract<E: Entry<Value = u16>>(make: impl FnOnce() -> E) -> u16 {
    make().read()
}

fn main() {
    assert_eq!(extract(|| Apply(Identity, Wrapped(Source(239)))), 239);
    assert_eq!(extract(|| Wrapped(Apply(Identity, Source(241)))), 241);
}
