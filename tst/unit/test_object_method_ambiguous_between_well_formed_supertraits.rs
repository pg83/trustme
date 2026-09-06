//@ compile-fail: multiple applicable items

// With `P: WithAssoc` in the environment both supertraits of `dyn Trait<P>`
// are well-formed routes to `method`, and upstream reports the call as
// ambiguous (E0283): a candidate is dropped for an obligation that cannot
// hold, never for merely naming a projection.

trait Supertrait<T> {
    fn method(&self) -> usize {
        1
    }
}
impl<T> Supertrait<T> for () {}

trait WithAssoc {
    type Assoc;
}
trait Trait<P: WithAssoc>: Supertrait<P::Assoc> + Supertrait<()> {}

fn call<P: WithAssoc>(x: &dyn Trait<P>) -> usize {
    x.method()
}

fn main() {}
