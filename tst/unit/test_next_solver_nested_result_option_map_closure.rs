//@ check-pass

static TABLE: &[(char, u32)] = &[('A', 0x61)];
static MULTI: &[[char; 3]] = &[['?', '\0', '\0']];

fn convert(input: char) -> [char; 3] {
    TABLE
        .binary_search_by(|&(key, _)| key.cmp(&input))
        .map(|index| {
            let value = TABLE[index].1;
            char::from_u32(value)
                .map(|character| [character, '\0', '\0'])
                .unwrap_or_else(|| unsafe { *MULTI.get_unchecked(0) })
        })
        .unwrap_or([input, '\0', '\0'])
}

fn main() {
    assert_eq!(convert('A'), ['a', '\0', '\0']);
}
