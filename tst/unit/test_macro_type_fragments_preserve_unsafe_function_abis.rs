macro_rules! call {
    ($ty:ty, $function:expr, $value:expr) => {{
        let function: $ty = $function;
        unsafe { function($value) }
    }};
}
macro_rules! assign {
    ($ty:ty, $value:expr) => {{ let function: $ty = $value; function }};
}
unsafe fn rust_step(value: u32) -> u32 { value + 3 }
unsafe extern "C" fn c_step(value: u32) -> u32 { value + 5 }
extern "C" fn safe_c_step(value: u32) -> u32 { value + 7 }
unsafe extern fn default_c_step(value: u32) -> u32 { value + 9 }
fn main() {
    assert_eq!(call!(unsafe fn(u32) -> u32, rust_step, 10), 13);
    assert_eq!(call!(unsafe extern "C" fn(u32) -> u32, c_step, 10), 15);
    assert_eq!(assign!(extern "C" fn(u32) -> u32, safe_c_step)(10), 17);
    assert_eq!(call!(unsafe extern fn(u32) -> u32, default_c_step, 10), 19);
}
