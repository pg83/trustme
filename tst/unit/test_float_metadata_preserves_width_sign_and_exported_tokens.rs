//@ aux-build: float_wire_values.rs
extern crate float_wire_values;

fn main() {
    assert_eq!(float_wire_values::HEAD.0.to_bits(), 0x3fa00000);
    assert_eq!(float_wire_values::HEAD.1.to_bits(), 0xc004000000000000);
    let first = float_wire_values::choose(true);
    assert_eq!(first.0.to_bits(), 0x400e000000000000);
    assert_eq!(first.1.to_bits(), 0x80000000);
    let second = float_wire_values::choose(false);
    assert_eq!(second.0.to_bits(), 0x401a000000000000);
    assert_eq!(second.1.to_bits(), 0x3f000000);
    assert_eq!(float_wire_values::fraction!().to_bits(), 0xbfc0000000000000);
}
