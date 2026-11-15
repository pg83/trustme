//@ compile-only
//@ compile-flags: -Z stop-after=mir -Z dump-mir
//@ output-contains: _3_mir.rs "=*m"=

fn main() {
    let mut output = 0u32;
    unsafe {
        llvm_asm!("" : "=*m"(&mut output as *mut u32));
    }
}
