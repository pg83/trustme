//@ compile-flags: -Cpanic=abort

fn main() {
    let mut value = 0_u32;
    'outer: loop {
        value += 1;
        match value {
            1 | 2 if { continue 'outer; } => (),
            _ => break 'outer,
        }
    }
    assert_eq!(value, 3);
}
