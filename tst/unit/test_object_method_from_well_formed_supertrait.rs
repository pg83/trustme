// `dyn Trait<P>` offers `method` through both of its supertraits,
// `Supertrait<<P as WithAssoc>::Assoc>` and `Supertrait<()>`.  In `call`, `P`
// carries no `WithAssoc` bound, so the projection in the first supertrait
// cannot be normalized in the caller's environment and upstream's method
// probe rejects that candidate (the probe's obligations fail); the second
// alone applies.  With a bound `P: WithAssoc` both apply and the call is
// ambiguous - the candidate is not dropped for being a projection.

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

fn call<P>(x: &dyn Trait<P>) -> usize {
    x.method()
}

fn main() {
    let _ = call::<()> as *const ();
}
