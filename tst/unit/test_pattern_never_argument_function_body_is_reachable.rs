#![feature(never_type)]

fn eliminate(value: !) -> u16 { match value {} }

fn main() {
    let function: fn(!) -> u16 = eliminate;
    assert_ne!(std::hint::black_box(function) as usize, 0);
}
