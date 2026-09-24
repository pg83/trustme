//@ aux-build: known_layout_aux.rs
// zerocopy-derive's `KnownLayout` for `AU16(u16)` emits an impl bounded by
// `<AU16 as Field<__Field_0>>::Type: KnownLayout`, i.e. `u16: KnownLayout`.
// rustc normalises the param env; a where-clause on a type with no generic
// parameters or inference variables is global, and a global where-clause does
// not shadow the impl, so `<u16 as KnownLayout>::PointerMetadata` normalises
// to `()`. We took any rigid projection self as non-global and let the
// clause shadow the impl: the projection stayed rigid and did not unify
// with `()`.
extern crate known_layout_aux as aux;
use core::ptr::NonNull;

#[repr(C, align(2))]
pub struct AU16(pub u16);

const _: () = {
    unsafe impl aux::KnownLayout for AU16 {
        type PointerMetadata = <u16 as aux::KnownLayout>::PointerMetadata;
        type MaybeUninit = __MaybeUninit;
        fn raw_from_ptr_len(bytes: NonNull<u8>, meta: <Self as aux::KnownLayout>::PointerMetadata) -> NonNull<Self> {
            let trailing = <u16 as aux::KnownLayout>::raw_from_ptr_len(bytes, meta);
            let slf = trailing.as_ptr() as *mut Self;
            unsafe { NonNull::new_unchecked(slf) }
        }
    }
    pub struct __Field_0;
    unsafe impl aux::Field<__Field_0> for AU16 {
        type Type = u16;
    }
    #[repr(C)]
    #[repr(align(2))]
    pub struct __MaybeUninit(
        core::mem::ManuallyDrop<<<AU16 as aux::Field<__Field_0>>::Type as aux::KnownLayout>::MaybeUninit>,
    )
    where
        <AU16 as aux::Field<__Field_0>>::Type: aux::KnownLayout;
    unsafe impl aux::KnownLayout for __MaybeUninit
    where
        <AU16 as aux::Field<__Field_0>>::Type: aux::KnownLayout,
    {
        type PointerMetadata = <AU16 as aux::KnownLayout>::PointerMetadata;
        type MaybeUninit = Self;
        fn raw_from_ptr_len(bytes: NonNull<u8>, meta: <Self as aux::KnownLayout>::PointerMetadata) -> NonNull<Self> {
            let trailing = <<<AU16 as aux::Field<__Field_0>>::Type as aux::KnownLayout>::MaybeUninit as aux::KnownLayout>::raw_from_ptr_len(bytes, meta);
            let slf = trailing.as_ptr() as *mut Self;
            unsafe { NonNull::new_unchecked(slf) }
        }
    }
};

fn main() {
    let mut v = AU16(3);
    let p = <AU16 as aux::KnownLayout>::raw_from_ptr_len(NonNull::from(&mut v).cast::<u8>(), ());
    assert_eq!(unsafe { p.as_ref() }.0, 3);
}
