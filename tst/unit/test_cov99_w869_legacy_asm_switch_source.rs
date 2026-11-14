//@ crate-type: lib
//@ compile-flags: -Cpanic=abort -Zmir-opt-level=3 -Zdump-mir

pub unsafe fn a_asm_switch(input: u32, choose: bool) -> u32 {
    let output: u32;
    llvm_asm!("" : "=r"(output) : "0"(input) : : "volatile");
    match output {
        0 => {
            if choose {
                return 10;
            } else {
                return 11;
            }
        }
        1 => return 12,
        _ => return 13,
    }
}

pub unsafe fn z_call(input: u32, choose: bool) -> u32 {
    a_asm_switch(input, choose)
}
