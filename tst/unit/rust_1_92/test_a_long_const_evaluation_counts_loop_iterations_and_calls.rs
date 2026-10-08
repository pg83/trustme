// zlib-rs and kuznyechik build lookup tables in constants, with loops that
// run about a million times. rustc's interpreter counts a step at each
// block that ends in a call or jumps back to a loop's head (the
// `ConstEvalCounter` that `CtfeLimit` inserts), and the deny-by-default
// `long_running_const_eval` lint stops it after two million steps. We
// stopped after four million blocks of any kind, which a loop body of a
// few blocks reaches long before rustc's limit.
const fn crc_table(rounds: u32) -> [u32; 256] {
    let mut table = [0u32; 256];
    let mut i = 0;
    while i < 256 {
        let mut c = i as u32;
        let mut k = 0;
        while k < rounds {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
            k += 1;
        }
        table[i] = c;
        i += 1;
    }
    table
}

const TABLE: [u32; 256] = crc_table(4000);

fn main() {
    assert_eq!(TABLE, crc_table(4000));
    assert_eq!(TABLE[0], 0);
}
