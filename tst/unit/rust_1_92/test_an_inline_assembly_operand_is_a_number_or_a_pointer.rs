//@ compile-fail: for inline assembly
// The Rust Reference's inline-assembly.md:580 example passes a struct as a
// register operand: `asm!("/* {} */", in(reg) x)` with `x: Foo`. rustc's
// `InlineAsmCtxt::check_asm_operand_type` takes integers, floats, thin
// pointers and references, function pointers and SIMD vectors, and reports
// "cannot use value of type `Foo` for inline assembly" for anything else.
// We had no such check; the case failed only because the C++ we generated
// named the zero-sized local it never declared.
struct Foo;

fn main() {
    #[cfg(target_arch = "x86_64")]
    {
        let x: Foo = Foo;
        unsafe { core::arch::asm!("/* {} */", in(reg) x); }
    }
    #[cfg(not(target_arch = "x86_64"))]
    core::compile_error!("an x86_64 example");
}
