//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Ignore<T> { type Out; }
impl<T> Ignore<T> for () { type Out = &'static dyn Send; }
impl<T> Ignore<T> for u16 { type Out = &'static dyn Send; }

fn accept<T, A: Ignore<T>>(_: &T, _: A::Out, _: A) {}
fn first() {}
fn second() {}

fn exercise(flag: bool) {
    accept::<_, _>(&(if flag { first } else { second }), &|| {}, ());
}

fn main() { exercise(true); exercise(false); }
