// image's `impl<T: ?Sized + ImageDecoder> ImageDecoder for Box<T>` forwards
// `read_image(self)` to `T::read_image_boxed(self)`, a `self: Box<Self>`
// method; with `T = dyn ImageDecoder` the call goes through the vtable,
// whose slot takes the receiver as `Box<()>`. That box is built in the
// caller only once the generic body is monomorphised and the call made
// virtual, after the first enumeration of what to translate: the second one
// found a `Box<()>` local whose drop glue nobody had enumerated.
pub trait Decoder {
    fn total(&self) -> u32;
    fn total_boxed(self: Box<Self>) -> u32;
}

pub struct Plain(u32);

impl Decoder for Plain {
    fn total(&self) -> u32 {
        self.0
    }
    fn total_boxed(self: Box<Self>) -> u32 {
        self.0 + 1
    }
}

impl<T: ?Sized + Decoder> Decoder for Box<T> {
    fn total(&self) -> u32 {
        (**self).total()
    }
    fn total_boxed(self: Box<Self>) -> u32 {
        T::total_boxed(*self)
    }
}

fn read<T: ?Sized + Decoder>(decoder: Box<T>) -> u32 {
    T::total_boxed(decoder)
}

fn main() {
    let decoder: Box<dyn Decoder> = Box::new(Plain(4));
    assert_eq!(decoder.total(), 4);
    assert_eq!(read(decoder), 5);
    let nested: Box<Box<dyn Decoder>> = Box::new(Box::new(Plain(6)));
    assert_eq!(nested.total_boxed(), 7);
}
