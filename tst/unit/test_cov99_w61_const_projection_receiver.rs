//@ crate-type: lib
//@ compile-fail: type annotations needed

struct Source;

trait Project<const N: usize> {
    type Out;
}

impl Project<0> for Source {
    type Out = u8;
}

impl Project<1> for Source {
    type Out = u16;
}

fn make<const N: usize>() -> <Source as Project<N>>::Out
where
    Source: Project<N>,
{
    loop {}
}

pub fn probe() {
    let _ = make().clone();
}
