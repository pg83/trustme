// An in+out *explicit register* is fully described by its "+" output; emitting
// a matching input for it too was rejected by clang.
fn main() {
    #[cfg(target_arch = "x86_64")]
    {
        let mut x: u64 = 40;
        unsafe { core::arch::asm!("add rax, 2", inout("rax") x); }
        assert_eq!(x, 42);
    }
}
