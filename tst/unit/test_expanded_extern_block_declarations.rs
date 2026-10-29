#[no_mangle]
pub extern "C" fn branch_add(left: i32, right: i32) -> i32 { left + right }
#[no_mangle]
pub static BRANCH_VALUE: i32 = 29;
macro_rules! declarations {
    () => {
        #[link_name = "branch_add"]
        pub fn add(left: i32, right: i32) -> i32;
        #[link_name = "BRANCH_VALUE"]
        pub static VALUE: i32;
    };
}
mod imported {
    extern "C" { declarations!(); }
}
fn local() -> i32 {
    macro_rules! block {
        () => { extern "C" { declarations!(); } };
    }
    block!();
    unsafe { add(VALUE, 5) }
}
fn main() {
    unsafe { assert_eq!(imported::add(imported::VALUE, 3), 32); }
    assert_eq!(local(), 34);
}
