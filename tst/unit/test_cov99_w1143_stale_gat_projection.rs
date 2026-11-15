//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Ignore { type Out<T>; }
impl Ignore for () { type Out<T> = &'static dyn Send; }
impl Ignore for u16 { type Out<T> = &'static dyn Sync; }
trait Wait<A> {}
impl<T, A> Wait<A> for T {}

fn accept<T: Wait<A>, A: Ignore>(_: &T, _: A::Out<T>, _: A) {}
fn first() {}
fn second() {}

fn exercise(flag: bool) {
    let marker = if flag { || () } else { || () };
    accept::<_, _>(&(if flag { first } else { second }), &|| {}, marker());
}

fn main() { exercise(true); exercise(false); }
