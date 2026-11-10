//@ aux-build: wide_slot_packet.rs
//@ input: aux/wide_slot_grid.rs

fn main() {
    let packet = wide_slot_packet::make().echo();
    assert_eq!(packet.data, (251u16, 257u32, 509u64, 1021u128));
    assert_eq!(packet.bytes, [29; 5]);
    assert_eq!(packet.indices(), [255, 256, 511, 5]);
    assert_eq!(core::mem::size_of_val(&packet.marker), 0);
}
