//@ check-pass
//@ crate-type: lib

use std::marker::PhantomData;

struct Wrap<Src, Dst>(Src, PhantomData<Dst>);

impl<Src, Dst> Wrap<Src, Dst> {
    fn new(src: Src) -> Self {
        Self(src, PhantomData)
    }
}

impl<'a, Src: ?Sized, Dst: ?Sized> Wrap<&'a mut Src, &'a mut Dst> {
    fn inference_helper(self) -> &'a mut Dst {
        loop {}
    }
}

struct Error<Src: ?Sized, Dst: ?Sized>(PhantomData<(*mut Src, *mut Dst)>);

impl<'a, Src, Dst> Wrap<&'a mut Src, &'a mut Dst> {
    fn convert(self) -> Result<&'a mut Dst, Error<Src, Dst>> {
        loop {}
    }
}

trait ConvertSrc<'a> {
    type Src: ?Sized;
}

trait ConvertDst<'a> {
    type Dst: ?Sized;

    fn convert(self) -> Result<&'a mut Self::Dst, Error<Self::Src, Self::Dst>>
    where
        Self: ConvertSrc<'a>;
}

impl<'a, Src: ?Sized, Dst: ?Sized> ConvertSrc<'a> for Wrap<&'a mut Src, &'a mut Dst> {
    type Src = Src;
}

impl<'a, Src: ?Sized, Dst: ?Sized> ConvertDst<'a> for Wrap<&'a mut Src, &'a mut Dst> {
    type Dst = Dst;

    fn convert(self) -> Result<&'a mut Dst, Error<Src, Dst>> {
        loop {}
    }
}

fn check() {
    let mut bytes = [0u8; 8];
    let wrapped = Wrap::new(&mut bytes);
    let _: Result<&mut [[u8; 2]], _> = if false {
        Ok(wrapped.inference_helper())
    } else {
        wrapped.convert()
    };
}
