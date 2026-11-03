#[repr(C)]
pub union Payload<T: Copy, const N: usize> {
    pub first: T,
    pub array: [T; N],
}

#[inline]
pub const fn array<T: Copy, const N: usize>(values: [T; N]) -> Payload<T, N> {
    Payload { array: values }
}

#[inline]
pub fn first<T: Copy>(value: T) -> Payload<T, 1> { Payload { first: value } }

#[inline]
pub fn replace<T: Copy, const N: usize>(payload: &mut Payload<T, N>, values: [T; N]) {
    *payload = Payload { array: values };
}
