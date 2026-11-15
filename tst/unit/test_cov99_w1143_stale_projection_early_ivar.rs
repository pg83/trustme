//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Ignore<T> { type Out; }
impl<T> Ignore<T> for () { type Out = u8; }
impl<T> Ignore<T> for u16 { type Out = u8; }

fn accept<A: Ignore<T>, T>(_: A, _: A::Out, _: T) {}
fn first() {}
fn second() {}

fn exercise(flag: bool) {
    accept::<_, _>((), *{
        let get = if flag { || &3u8 } else { || &5u8 };
        get()
    }, if flag { first } else { second });
}

fn main() { exercise(true); exercise(false); }
