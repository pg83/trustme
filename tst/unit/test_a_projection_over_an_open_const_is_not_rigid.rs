// generic-array's `GenericArray::into_array<const U: usize>(self) -> [T; U]
// where Const<U>: IntoArrayLength<ArrayLength = N>`, called as `let [a] =
// array.into_array()`. Its impl `IntoArrayLength for Const<N> where U<N>:
// ArrayLength` names `typenum::U<N> = <Const<N> as ToUInt>::Output` (`UOf` here); with `U`
// still open that projection cannot be normalized - upstream reads it as a
// fresh variable and `?: ArrayLength` as ambiguous. The check for open inputs
// looked at type variables only, so a projection over an open const was read
// as rigid, `ArrayLength` found no candidate for it, and the method was
// rejected: "No applicable methods".
use std::marker::PhantomData;

pub trait Unsigned {
    const USIZE: usize;
}
pub struct UTerm;
pub struct B0;
pub struct B1;
pub struct UInt<U, B>(PhantomData<(U, B)>);
pub trait Bit {
    const U: usize;
}
impl Bit for B0 {
    const U: usize = 0;
}
impl Bit for B1 {
    const U: usize = 1;
}
impl Unsigned for UTerm {
    const USIZE: usize = 0;
}
impl<U: Unsigned, B: Bit> Unsigned for UInt<U, B> {
    const USIZE: usize = U::USIZE * 2 + B::U;
}

pub trait ArrayLength: Unsigned + 'static {}
impl ArrayLength for UTerm {}
impl<U: ArrayLength, B: Bit + 'static> ArrayLength for UInt<U, B> {}

pub type U1 = UInt<UTerm, B1>;
pub type U2 = UInt<UInt<UTerm, B1>, B0>;

pub struct Const<const N: usize>;
pub trait ToUInt {
    type Output;
}
impl ToUInt for Const<0> {
    type Output = UTerm;
}
impl ToUInt for Const<1> {
    type Output = U1;
}
impl ToUInt for Const<2> {
    type Output = U2;
}
pub type UOf<const N: usize> = <Const<N> as ToUInt>::Output;

pub trait IntoArrayLength {
    type ArrayLength: ArrayLength;
}
impl<const N: usize> IntoArrayLength for Const<N>
where
    Const<N>: ToUInt,
    UOf<N>: ArrayLength,
{
    type ArrayLength = UOf<N>;
}
impl<N: ArrayLength> IntoArrayLength for N {
    type ArrayLength = N;
}

pub struct GenericArray<T, N: ArrayLength> {
    data: Vec<T>,
    _n: PhantomData<N>,
}

impl<T, N: ArrayLength> GenericArray<T, N> {
    pub fn from_array<const U: usize>(value: [T; U]) -> Self
    where
        Const<U>: IntoArrayLength<ArrayLength = N>,
    {
        GenericArray { data: Vec::from(value), _n: PhantomData }
    }

    pub fn into_array<const U: usize>(self) -> [T; U]
    where
        Const<U>: IntoArrayLength<ArrayLength = N>,
    {
        match self.data.try_into() {
            Ok(array) => array,
            Err(_) => unreachable!(),
        }
    }
}

impl<T> From<GenericArray<T, U1>> for (T,) {
    fn from(array: GenericArray<T, U1>) -> Self {
        let [a] = array.into_array();
        (a,)
    }
}

impl<T> From<GenericArray<T, U2>> for (T, T) {
    fn from(array: GenericArray<T, U2>) -> Self {
        let [a, b] = array.into_array();
        (a, b)
    }
}

fn main() {
    let one: GenericArray<u8, U1> = GenericArray::from_array([7]);
    let (a,) = <(u8,)>::from(one);
    assert_eq!(a, 7);
    let two: GenericArray<u8, U2> = GenericArray::from_array([1, 2]);
    assert_eq!(<(u8, u8)>::from(two), (1, 2));
    assert_eq!(<U2 as Unsigned>::USIZE, 2);
}
