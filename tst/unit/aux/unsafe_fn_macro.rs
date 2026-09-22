#[macro_export]
macro_rules! declare_unsafe {
    ($name:ident $body:block) => {
        #[allow(dead_code)]
        unsafe fn $name() -> u32 $body
    };
}
