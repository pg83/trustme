//@ compile-fail: Type mismatch
trait Project<A> { type Out; }
struct Wrap<G,A>(G,A);
trait Adapt { type Out; }
impl<G: Project<A>,A> Adapt for Wrap<G,A> { type Out = <G as Project<A>>::Out; }
fn require<C: Adapt<Out=R>,R>(_:C) {}
fn check<G: Project<u8>+Project<u16>>(value: G) {
 require::<Wrap<G,u8>,<G as Project<u16>>::Out>(Wrap(value,0));
}
fn main() {}
