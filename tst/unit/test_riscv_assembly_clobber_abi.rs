//@ crate-type: lib
//@ compile-flags: --target=riscv64-unknown-linux-gnu -Cemit-cpp-only
//@ output-contains: .cpp "x31"
//@ output-contains: .cpp "f31"
//@ output-contains: .cpp "v31"
pub unsafe fn call_boundary(value: usize) -> usize {
    let output: usize;
    std::arch::asm!("addi a0, a0, 1", inlateout("a0") value => output, clobber_abi("C"));
    output
}
pub unsafe fn register_classes(mut value: usize, mut floating: f64) -> (usize, f64) {
    std::arch::asm!("addi {value}, {value}, 1", "fadd.d {floating}, {floating}, {floating}",
        value = inout(reg) value, floating = inout(freg) floating, options(nomem, nostack));
    (value, floating)
}
