pub trait Value { fn value(&self) -> u16; }
impl Value for u16 { fn value(&self) -> u16 { *self } }
pub trait Derived: Value + Send {}
impl Derived for u16 {}
pub static NUMBER: u16 = 371;
pub const fn remove_sync(value: &(dyn Value + Sync)) -> &dyn Value { value }
pub const fn upcast(value: &dyn Derived) -> &dyn Value { value }
pub const fn borrow_static() -> &'static dyn Value { &NUMBER }
pub const fn weaken_callback(value: fn(u16) -> u16) -> unsafe fn(u16) -> u16 {
    value as unsafe fn(u16) -> u16
}
pub const fn upcast_raw(value: *const dyn Derived) -> *const dyn Value { value }
pub trait Measure<T> { fn measure(&self, extra: T) -> u16; }
impl Measure<u8> for u16 { fn measure(&self, extra: u8) -> u16 { *self + extra as u16 } }
impl Measure<u32> for u16 { fn measure(&self, extra: u32) -> u16 { *self + extra as u16 + 1000 } }
pub trait Both: Measure<u8> + Measure<u32> {}
impl Both for u16 {}
pub const fn choose_small(value: &dyn Both) -> &dyn Measure<u8> { value }
pub const fn choose_large(value: &dyn Both) -> &dyn Measure<u32> { value }
pub trait Associated<T, const N: u16> {
    type Output;
    fn apply(&self, value: T) -> Self::Output;
}
impl Associated<u8, 7> for u16 {
    type Output = u32;
    fn apply(&self, value: u8) -> u32 { *self as u32 + value as u32 + 7 }
}
pub trait WithAssociated: Associated<u8, 7, Output = u32> {}
impl WithAssociated for u16 {}
pub const fn associated(value: &dyn WithAssociated) -> &dyn Associated<u8, 7, Output = u32> { value }
pub trait OpenAssociated: Associated<u8, 7> {}
impl OpenAssociated for u16 {}
pub const fn open_associated(value: &dyn OpenAssociated<Output = u32>) -> &dyn Associated<u8, 7, Output = u32> { value }
pub trait Consume {
    fn consume(self) -> u16;
    #[track_caller]
    fn peek(&self) -> u16;
}
impl Consume for u16 {
    fn consume(self) -> u16 { self }
    #[track_caller]
    fn peek(&self) -> u16 { *self }
}
pub trait WithConsume: Consume {}
impl WithConsume for u16 {}
pub const fn consuming(value: &dyn WithConsume) -> &dyn Consume { value }
