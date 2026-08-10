// Extracted from src/inline-assembly.md:1406
#[cfg(target_arch = "x86_64")]
pub fn fadd(x: f64, y: f64) -> f64 {
  let mut out = 0f64;
  let mut top = 0u16;
  // we can do complex stuff with x87 if we clobber the entire x87 stack
  unsafe { core::arch::asm!(
    "fld qword ptr [{x}]",
    "fld qword ptr [{y}])",
    "faddp",
    "fstp qword ptr [{out}]",
    "xor eax, eax",
    "fstsw ax",
    "shl eax, 11",
    x = in(reg) &x,
    y = in(reg) &y,
    out = in(reg) &mut out,
    out("st(0)") _, out("st(1)") _, out("st(2)") _, out("st(3)") _,
    out("st(4)") _, out("st(5)") _, out("st(6)") _, out("st(7)") _,
    out("eax") top
  );}

  assert_eq!(top & 0x7, 0);
  out
}

pub fn main() {
#[cfg(target_arch = "x86_64")]{
  assert_eq!(fadd(1.0, 1.0), 2.0);
}
}
