//@ edition: 2021
//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn plus(n: usize) -> usize {
    n + 1
}
trait Erase {
    type Value;
}
impl<T> Erase for T {
    type Value = ();
}
struct Wrap<T: Erase>(T::Value);
struct Factory;

impl Factory {
    fn make<T, const N: usize>(&self, _value: T) -> Wrap<[u8; plus(N)]>
    where
        [(); plus(N)]:,
    {
        Wrap::<[u8; plus(N)]>(())
    }
}

fn constrain(_: Option<u8>) {}

fn transfer<const N: usize>() -> Wrap<[u8; plus(N)]>
where
    [(); plus(N)]:,
{
    let marker = None;
    let result: Wrap<[u8; plus(N)]> = Factory.make::<_, N>(marker);
    constrain(marker);
    result
}
