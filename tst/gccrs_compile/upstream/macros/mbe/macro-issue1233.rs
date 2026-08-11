// { dg-additional-options "-frust-cfg=A -w" }


macro_rules! impl_uint {
    ($($ty:ident = $lang:literal),*) => {
        $(
            impl ToLeExt for $ty {
                fn to_le_ext(self) -> Self {
                    #[cfg(not(A))]
                    {
                        self
                    }
                    #[cfg(A)]
                    {
                        self
                    }
                }
            }
        )*
    }
}

trait ToLeExt {
    fn to_le_ext(self) -> Self;
}

impl_uint!(u8 = "u8", u16 = "u16", u32 = "u32");
