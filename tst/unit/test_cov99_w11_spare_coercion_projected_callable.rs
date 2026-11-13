use std::marker::PhantomData;

trait Input { type Item; }
struct Seed;
impl Input for Seed { type Item = u8; }

struct Transform<P, F> { function: F, input: PhantomData<P> }
trait Step {
    type Item;
    fn step(&mut self, value: u8) -> Self::Item;
}
impl<P, F, O, E> Step for Transform<P, F>
where P: Input<Item = u8>, F: FnMut(P::Item) -> Result<O, E> {
    type Item = Result<O, E>;
    fn step(&mut self, value: u8) -> Self::Item { (self.function)(value) }
}
fn build<P, F>(function: F) -> Transform<P, F> {
    Transform { function, input: PhantomData }
}

fn main() {
    let offset = 1u16;
    let mut transform = build::<Seed, _>(|value| Ok(value as u16 + offset));
    let object: &mut dyn Step<Item = Result<u16, ()>> = &mut transform;
    assert_eq!(object.step(40), Ok(41));
}
