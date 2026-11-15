//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Project<I> {
    type Output;
}

struct Lift;

impl<I, T> Project<I> for Lift
where
    I: Iterator<Item = T>,
{
    type Output = Vec<T>;
}

fn project<I: Iterator>(_: &I) -> <Lift as Project<I>>::Output {
    Vec::new()
}

fn main() {
    let iter = Default::default();
    let vector = project(&iter);
    let _: Vec<u8> = vector;
    let _: std::iter::Empty<u8> = iter;
    let _ = Some(()).unwrap();
}
