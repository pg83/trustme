//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Ignore { type Out<T>; }
impl Ignore for () { type Out<T> = &'static dyn Send; }
impl Ignore for u16 { type Out<T> = &'static dyn Sync; }

fn accept<T: Copy, A: Ignore>(_: &T, _: A::Out<T>, _: A) {}

fn exercise(flag: bool) {
    let marker = if flag { || () } else { || () };
    accept::<_, _>(&(if flag { || () } else { || () }), &|| {}, marker());
}

fn main() { exercise(true); exercise(false); }
