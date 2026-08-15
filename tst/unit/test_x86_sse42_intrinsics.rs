#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sse4.2")]
unsafe fn bytes(value: &[u8]) -> __m128i {
    let mut result = [0_u8; 16];
    result[..value.len()].copy_from_slice(value);
    core::mem::transmute(result)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sse4.2")]
unsafe fn test_sse42() {
    assert_eq!(_mm_crc32_u8(0x2aa1_e72b, 0x2a), 0xf241_22e4);
    assert_eq!(_mm_crc32_u16(0x8ece_c3b5, 0x022b), 0x013b_b2fb);
    assert_eq!(_mm_crc32_u32(0x0bad_eafe, 0xc0fe_beef), 0xb309_502f);
    assert_eq!(_mm_crc32_u64(0x0007_819d_ccd3_e824, 0x0000_02a2_2b84_5fed), 0xbb6c_dc6c);

    let mask: [u8; 16] = core::mem::transmute(_mm_cmpistrm::<{ _SIDD_CMP_EQUAL_ANY | _SIDD_UNIT_MASK }>(
        bytes(b"ABC"),
        bytes(b"ABC UVW XYZ"),
    ));
    assert_eq!(mask, [u8::MAX, u8::MAX, u8::MAX, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

    assert_eq!(
        _mm_cmpistri::<{ _SIDD_CMP_EQUAL_ORDERED | _SIDD_LEAST_SIGNIFICANT }>(
            bytes(b"Hello"),
            bytes(b"  Hello"),
        ),
        2,
    );
    assert_eq!(
        _mm_cmpistri::<{ _SIDD_CMP_EQUAL_EACH | _SIDD_MOST_SIGNIFICANT }>(
            bytes(b"Hello"),
            bytes(b"Hello Hello H"),
        ),
        15,
    );

    let ranges: [u16; 8] = core::mem::transmute(_mm_cmpestrm::<
        { _SIDD_SWORD_OPS | _SIDD_CMP_RANGES | _SIDD_UNIT_MASK },
    >(
        _mm_setr_epi16(0, 1, 7, 8, 0, 0, -100, 100),
        8,
        _mm_setr_epi16(1, 2, 3, 4, 5, 6, 7, 8),
        8,
    ));
    assert_eq!(ranges, [u16::MAX; 8]);

    assert_eq!(_mm_cmpistrz::<_SIDD_CMP_EQUAL_ORDERED>(bytes(b""), bytes(b"Hello")), 1);
    assert_eq!(_mm_cmpistrs::<_SIDD_CMP_EQUAL_ORDERED>(bytes(b"Hello"), bytes(b"")), 1);
    assert_eq!(_mm_cmpistrc::<_SIDD_UNIT_MASK>(bytes(b"                "), bytes(b"       !        ")), 1);
    assert_eq!(_mm_cmpistro::<_SIDD_UBYTE_OPS>(bytes(b"Hello"), bytes(b"World")), 0);
    assert_eq!(_mm_cmpistra::<_SIDD_UNIT_MASK>(bytes(b""), bytes(b"Hello!!!!!!!!!!!")), 1);

    assert_eq!(
        _mm_cmpestri::<_SIDD_CMP_EQUAL_ORDERED>(bytes(b"bar"), 3, bytes(b"foobar"), 6),
        3,
    );
    assert_eq!(_mm_cmpestrz::<_SIDD_CMP_EQUAL_ORDERED>(bytes(b""), 16, bytes(b"Hello"), 6), 1);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_sse42();
    }
}
