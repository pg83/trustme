//@ compile-fail: inline assembly is not allowed in constant evaluation

const VALUE: u16 = {
    unsafe { core::arch::asm!("", options(nomem, nostack)); }
    19
};

fn main() {
    let _ = VALUE;
}
