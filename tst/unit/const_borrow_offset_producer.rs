pub static BASE: (u8, u32) = (9, 0x1234_5678);
pub const FIELD: &'static u32 = &BASE.1;

pub fn field<T>() -> &'static u32 {
    let _ = core::marker::PhantomData::<T>;
    FIELD
}
