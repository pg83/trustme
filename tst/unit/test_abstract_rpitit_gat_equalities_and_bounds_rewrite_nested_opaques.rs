//@ compile-only
//@ compile-fail: nested `impl Trait` is not allowed
trait Family<T> {
    type Member<U>;
}

trait Carrier<T>: Family<T> {}

trait Api {
    fn equality(&self) -> impl Carrier<impl Copy, Member<impl Clone> = u8>;
    fn bounded(&self) -> impl Carrier<impl Copy, Member<impl Clone>: Send>;
}

fn main() {}
