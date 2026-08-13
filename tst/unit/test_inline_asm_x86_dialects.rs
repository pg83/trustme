#[cfg(target_arch = "x86_64")]
fn main() {
    use core::arch::asm;

    let mut incremented = 4u64;
    unsafe {
        asm!("inc {value}", value = inout(reg) incremented);
    }
    assert_eq!(incremented, 5);

    let narrow_input = 9u32;
    let full_output: u64;
    unsafe {
        asm!(
            "mov {output:r}, {input:r}",
            input = in(reg) narrow_input,
            output = lateout(reg) full_output,
        );
    }
    assert_eq!(full_output & u32::MAX as u64, 9);

    let narrow_output: u32;
    unsafe {
        asm!(
            "mov {output:e}, {input:e}",
            input = in(reg) 11u32,
            output = lateout(reg) narrow_output,
        );
    }
    assert_eq!(narrow_output, 11);

    let explicit_output: u32;
    unsafe {
        asm!(
            "mov eax, {input:e}",
            input = in(reg) 13u32,
            lateout("eax") explicit_output,
        );
    }
    assert_eq!(explicit_output, 13);

    let att_output: u32;
    unsafe {
        asm!(
            "movl {input:e}, {output:e}",
            input = in(reg) 17u32,
            output = lateout(reg) att_output,
            options(att_syntax),
        );
    }
    assert_eq!(att_output, 17);

    let memory = 19u32;
    let loaded: u32;
    unsafe {
        asm!(
            "mov {output:e}, dword ptr [{address}]",
            address = in(reg) &memory,
            output = lateout(reg) loaded,
            options(readonly),
        );
    }
    assert_eq!(loaded, 19);
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
