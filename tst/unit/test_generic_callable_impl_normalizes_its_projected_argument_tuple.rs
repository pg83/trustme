#![feature(fn_traits, unboxed_closures, tuple_trait)]

trait Signature {
    type Args;
}

struct Unary;

impl Signature for Unary {
    type Args = (u16,);
}

struct Handler<S>(u16, std::marker::PhantomData<S>);

impl<S: Signature> FnOnce<S::Args> for Handler<S>
where
    S::Args: std::marker::Tuple,
{
    type Output = u16;

    extern "rust-call" fn call_once(self, _: S::Args) -> u16 {
        self.0
    }
}

fn main() {
    let handler = Handler::<Unary>(251, std::marker::PhantomData);
    assert_eq!(handler(257u16), 251);
}
