//@ compile-fail: error:
use std::marker::PhantomData;

trait Local {}
trait Family {
    type Item: ?Sized;
}

struct First;
struct Second;
impl Family for First {
    type Item = dyn Local;
}
impl Family for Second {
    type Item = u32;
}

struct Envelope<T: ?Sized>(PhantomData<T>);
trait Decode {
    type Output;
}
impl Decode for Envelope<dyn Local> {
    type Output = u8;
}
impl<T: ?Sized> Decode for Envelope<T>
where
    u16: PartialEq<T>,
{
    type Output = u16;
}

fn main() {
    let _: Option<<Envelope<<_ as Family>::Item> as Decode>::Output> = None;
}
