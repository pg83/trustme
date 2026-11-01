//@ aux-build: asm_operand_exports.rs
//@ compile-variant: -O
use asm_operand_exports::value as imported;

fn main() {
    let data: usize;
    let function: usize;
    let generic: usize;
    let named: usize;
    unsafe {
        core::arch::asm!("lea {output}, [rip + {symbol}]", output = out(reg) data,
                         symbol = sym asm_operand_exports::DATA, options(nomem, nostack, preserves_flags));
        core::arch::asm!("lea {function}, [rip + {symbol}]", "lea {named}, [rip + {renamed}]",
                         function = out(reg) function, named = out(reg) named,
                         symbol = sym imported, renamed = sym asm_operand_exports::renamed,
                         options(nomem, nostack, preserves_flags));
        core::arch::asm!("lea {output}, [rip + {symbol}]", output = out(reg) generic,
                         symbol = sym asm_operand_exports::width::<u16>, options(nomem, nostack, preserves_flags));
        assert_eq!(*(data as *const u32), 37);
        let call: fn() -> u32 = std::mem::transmute(function);
        assert_eq!(call(), 41);
        let renamed: extern "C" fn() -> u32 = std::mem::transmute(named);
        assert_eq!(renamed(), 43);
    }
    assert_eq!(data, &asm_operand_exports::DATA as *const u32 as usize);
    assert_eq!(function, imported as usize);
    assert_eq!(named, asm_operand_exports::renamed as usize);
    assert_eq!(generic, asm_operand_exports::width::<u16> as usize);
    assert_eq!(asm_operand_exports::width::<u16>(), 2);
}
