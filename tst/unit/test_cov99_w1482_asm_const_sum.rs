fn main() {
    unsafe {
        core::arch::asm!("/* {value} */", value = const 1usize + 2usize);
    }
}
