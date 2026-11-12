#[repr(i16)]
enum Signed {
    Low = -1291,
    Middle = 0,
    High = 1297,
}

#[repr(u64)]
enum Wide {
    Low = 0x8000_0000_0000_0517,
    High = 0xffff_ffff_ffff_fffb,
}

#[inline(never)]
fn signed_value(value: Signed) -> i16 {
    match value {
        Signed::Low => -1291,
        Signed::Middle => 0,
        Signed::High => 1297,
    }
}

#[inline(never)]
fn wide_value(value: Wide) -> u64 {
    match value {
        Wide::Low => 0x8000_0000_0000_0517,
        Wide::High => 0xffff_ffff_ffff_fffb,
    }
}

fn main() {
    assert_eq!(signed_value(Signed::Low), -1291);
    assert_eq!(signed_value(Signed::Middle), 0);
    assert_eq!(signed_value(Signed::High), 1297);
    assert_eq!(wide_value(Wide::Low), Wide::Low as u64);
    assert_eq!(wide_value(Wide::High), Wide::High as u64);
}
