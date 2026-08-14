#![feature(extended_varargs_abi_support)]

use std::arch::global_asm;

extern "sysv64" {
    fn sum_sysv64(first: u32, rest: ...) -> u32;
}

extern "win64" {
    fn sum_win64(first: u32, rest: ...) -> u32;
}

global_asm!(
    ".globl sum_sysv64",
    ".type sum_sysv64,@function",
    "sum_sysv64:",
    "lea eax, [edi + esi]",
    "add eax, edx",
    "ret",
    ".globl sum_win64",
    ".type sum_win64,@function",
    "sum_win64:",
    "lea eax, [ecx + edx]",
    "add eax, r8d",
    "ret",
);

fn main() {
    unsafe {
        assert_eq!(sum_sysv64(1, 2, 3), 6);
        assert_eq!(sum_win64(1, 2, 3), 6);
    }
}
