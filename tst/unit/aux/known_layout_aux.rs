use core::ptr::NonNull;

pub unsafe trait KnownLayout {
    type PointerMetadata;
    type MaybeUninit: ?Sized + KnownLayout<PointerMetadata = Self::PointerMetadata>;
    fn raw_from_ptr_len(bytes: NonNull<u8>, meta: Self::PointerMetadata) -> NonNull<Self>;
}

pub unsafe trait Field<Field> {
    type Type: ?Sized;
}

const _: () = {
    unsafe impl KnownLayout for u16 {
        type PointerMetadata = ();
        type MaybeUninit = core::mem::MaybeUninit<Self>;
        fn raw_from_ptr_len(bytes: NonNull<u8>, _meta: ()) -> NonNull<Self> {
            bytes.cast::<Self>()
        }
    }
};

const _: () = {
    unsafe impl<T> KnownLayout for core::mem::MaybeUninit<T> {
        type PointerMetadata = ();
        type MaybeUninit = core::mem::MaybeUninit<Self>;
        fn raw_from_ptr_len(bytes: NonNull<u8>, _meta: ()) -> NonNull<Self> {
            bytes.cast::<Self>()
        }
    }
};
