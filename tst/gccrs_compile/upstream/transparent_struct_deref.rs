#[repr(transparent)]
pub struct Foo {
    inner: i32,
}

impl Foo {
    pub const fn to_ptr(&self) -> *const i32 {
        &self.inner
    }
}

pub fn read(value: &Foo) -> i32 {
    unsafe { *value.to_ptr() }
}
