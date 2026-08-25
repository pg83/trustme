//@ check-pass
//@ crate-type: lib

use std::marker::PhantomData;

struct Wrap<'a, Src: ?Sized, Dst: ?Sized>(&'a mut Src, PhantomData<&'a mut Dst>);

impl<'a, Src: ?Sized, Dst: ?Sized> Wrap<'a, Src, Dst> {
    fn new(src: &'a mut Src) -> Self {
        Self(src, PhantomData)
    }

    fn inference_helper(self) -> &'a mut Dst {
        loop {}
    }
}

impl<'a, Src, Dst> Wrap<'a, Src, Dst> {
    fn convert(self) -> &'a mut Dst {
        loop {}
    }
}

trait ConvertDst<'a> {
    type Dst: ?Sized;

    fn convert(self) -> &'a mut Self::Dst;
}

impl<'a, Src: ?Sized, Dst: ?Sized> ConvertDst<'a> for Wrap<'a, Src, Dst> {
    type Dst = Dst;

    fn convert(self) -> &'a mut Dst {
        loop {}
    }
}

fn check() {
    let mut bytes = [0u8; 8];
    let wrapped = Wrap::new(&mut bytes);
    let _: &mut [[u8; 2]] = if false {
        wrapped.inference_helper()
    } else {
        wrapped.convert()
    };
}
