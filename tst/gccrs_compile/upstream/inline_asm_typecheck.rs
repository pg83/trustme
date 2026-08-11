
#![feature(rustc_attrs)]

fn main() {
    let mut _num1: i32 = 10;
    let mut _num2: i32 = 10;
    unsafe {
        // This demonstrates that std::arch::asm!'s is inferred with a unit type is parsed correctly.
        let _ = std::arch::asm!("nop");

        // The std::arch::asm! block never returns, and its return type is defined as ! (never).
        // Behavior is undefined if execution falls through past the end of the asm code.
        // A noreturn asm block behaves just like a function which doesn't return; notably, local variables in scope are not dropped before it is invoked.
        let _ = std::arch::asm!("nop", options(noreturn));
    }
}
