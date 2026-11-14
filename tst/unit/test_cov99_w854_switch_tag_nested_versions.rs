//@ aux-build: cov99_w854_nested_signed/cov99_w854_nested_store.rs
//@ aux-output-dir: cov99_w854_nested_signed/cov99_w854_nested_store.rs=signed
//@ aux-build: cov99_w854_old_nested_switch.rs
//@ aux-build: cov99_w854_nested_unsigned/cov99_w854_nested_store.rs
//@ aux-output-dir: cov99_w854_nested_unsigned/cov99_w854_nested_store.rs=unsigned
//@ compile-flags: -C opt-level=2 -Zmir-opt-level=2

#[inline(never)]
fn choose(old: bool) -> u8 {
    unsafe {
        if old {
            cov99_w854_old_nested_switch::read(0)
        } else {
            match cov99_w854_nested_store::SLOT {
                0 => 7,
                1 => 8,
                _ => 9,
            }
        }
    }
}

fn main() {
    assert_eq!(choose(true), 8);
    assert_eq!(choose(false), 8);
}
