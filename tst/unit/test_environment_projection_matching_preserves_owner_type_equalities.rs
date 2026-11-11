use std::marker::PhantomData;

trait Read {
    type Output;
    fn read(self) -> Self::Output;
}

struct Holder<T>(PhantomData<T>);

fn exchange<T>(value: Holder<T>) -> <Holder<T> as Read>::Output
where
    Holder<T>: Read,
{
    value.read()
}

fn projected<T>() -> u16
where
    Holder<T>: Read<Output = u16>,
{
    exchange(Holder(PhantomData))
}

fn main() {}
