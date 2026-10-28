//@ compile-flags: -Zdump-ast -Zdump-hir -Zdump-mir
//@ output-contains: _1_ast.rs asm!(
//@ output-contains: _2_hir.rs asm!()
//@ output-contains: _3_mir.rs jmp
#[cfg(target_arch = "x86_64")]
fn main() {
    let mut first = false;
    unsafe {
        core::arch::asm!("jmp {}", label {
            first = true;
        });
    }
    assert!(first);

    let mut second = false;
    unsafe {
        core::arch::asm!("jmp {}", label {
            second = true;
        }, options(noreturn));
    }
    assert!(second);
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
