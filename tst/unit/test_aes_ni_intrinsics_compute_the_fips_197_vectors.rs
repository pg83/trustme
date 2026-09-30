//@ run-pass
// aes's tests reach `_mm_aeskeygenassist_si128`, `_mm_aesenc_si128` and the
// rest once `aes` is detected, and those are `llvm.x86.aesni.*` intrinsics,
// which LLVM emits as the AES-NI instructions. They were `abort()`. FIPS-197
// appendix C.1 (AES-128) and A.1 (the expanded key's last round key).
#[cfg(target_arch = "x86_64")]
mod aes128 {
    use std::arch::x86_64::*;

    #[target_feature(enable = "aes,sse2")]
    fn assist(key: __m128i, generated: __m128i) -> __m128i {
        let generated = _mm_shuffle_epi32::<0xff>(generated);
        let mut key = key;
        key = _mm_xor_si128(key, _mm_slli_si128::<4>(key));
        key = _mm_xor_si128(key, _mm_slli_si128::<4>(key));
        key = _mm_xor_si128(key, _mm_slli_si128::<4>(key));
        _mm_xor_si128(key, generated)
    }

    macro_rules! next {
        ($key:expr, $rcon:literal) => {
            assist($key, _mm_aeskeygenassist_si128::<$rcon>($key))
        };
    }

    #[target_feature(enable = "aes,sse2")]
    pub fn schedule(key: __m128i) -> [__m128i; 11] {
        let mut keys = [key; 11];
        keys[1] = next!(keys[0], 0x01);
        keys[2] = next!(keys[1], 0x02);
        keys[3] = next!(keys[2], 0x04);
        keys[4] = next!(keys[3], 0x08);
        keys[5] = next!(keys[4], 0x10);
        keys[6] = next!(keys[5], 0x20);
        keys[7] = next!(keys[6], 0x40);
        keys[8] = next!(keys[7], 0x80);
        keys[9] = next!(keys[8], 0x1b);
        keys[10] = next!(keys[9], 0x36);
        keys
    }

    #[target_feature(enable = "aes,sse2")]
    pub fn encrypt(keys: &[__m128i; 11], block: __m128i) -> __m128i {
        let mut state = _mm_xor_si128(block, keys[0]);
        for key in &keys[1..10] {
            state = _mm_aesenc_si128(state, *key);
        }
        _mm_aesenclast_si128(state, keys[10])
    }

    #[target_feature(enable = "aes,sse2")]
    pub fn decrypt(keys: &[__m128i; 11], block: __m128i) -> __m128i {
        let mut state = _mm_xor_si128(block, keys[10]);
        for key in keys[1..10].iter().rev() {
            state = _mm_aesdec_si128(state, _mm_aesimc_si128(*key));
        }
        _mm_aesdeclast_si128(state, keys[0])
    }

    pub fn load(bytes: &[u8; 16]) -> __m128i {
        unsafe { _mm_loadu_si128(bytes.as_ptr() as *const __m128i) }
    }

    pub fn store(value: __m128i) -> [u8; 16] {
        let mut bytes = [0u8; 16];
        unsafe { _mm_storeu_si128(bytes.as_mut_ptr() as *mut __m128i, value) };
        bytes
    }
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("aes") {
        use aes128::*;
        let key = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f];
        let plain = [0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff];
        let cipher = [0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a];
        let fips_key = [0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c];
        let fips_last = [0xd0, 0x14, 0xf9, 0xa8, 0xc9, 0xee, 0x25, 0x89, 0xe1, 0x3f, 0x0c, 0xc8, 0xb6, 0x63, 0x0c, 0xa6];
        unsafe {
            let keys = schedule(load(&key));
            let encrypted = encrypt(&keys, load(&plain));
            assert_eq!(store(encrypted), cipher);
            assert_eq!(store(decrypt(&keys, encrypted)), plain);
            assert_eq!(store(schedule(load(&fips_key))[10]), fips_last);
        }
    }
}
