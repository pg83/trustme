#![crate_type = "rlib"]

pub trait Reader {
    type Offset;
}

pub struct Unit<R, Offset = <R as Reader>::Offset>
where
    R: Reader<Offset = Offset>,
    Offset: Copy,
{
    pub marker: core::marker::PhantomData<(R, Offset)>,
}
