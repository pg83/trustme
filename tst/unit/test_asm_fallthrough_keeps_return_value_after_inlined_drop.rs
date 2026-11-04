//@ compile-variant: -O
struct Gate(usize);
impl Drop for Gate {
    #[inline(always)]
    fn drop(&mut self) {
        unsafe {
            core::arch::asm!("test {flag}, {flag}", "jnz {stop}",
                flag = in(reg) self.0, stop = label { std::process::exit(79); },
                options(nostack));
        }
    }
}
#[inline(never)]
fn calculate(input: u32) -> u32 {
    let _gate = Gate(std::hint::black_box(0));
    input.wrapping_mul(3).wrapping_add(11)
}
fn main() {
    assert_eq!(calculate(std::hint::black_box(127)), 392);
    assert_eq!(calculate(std::hint::black_box(u32::MAX)), 8);
}
