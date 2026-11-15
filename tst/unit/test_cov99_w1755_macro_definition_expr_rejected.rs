//@ compile-fail: Macro didn't expand to anything
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    (macro_rules! local { () => {} },) = ((),);
}
