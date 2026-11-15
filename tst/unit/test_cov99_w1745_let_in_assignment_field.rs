//@ compile-fail: invalid left-hand side of assignment
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Slot(u8);

fn main() {
    Slot { 0: let _ = 0u8 } = Slot(1u8);
}
