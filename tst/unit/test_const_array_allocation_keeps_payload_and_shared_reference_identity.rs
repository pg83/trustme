const fn build() -> [u16; 4] {
    let mut value = [0; 4];
    let mut index = 0;
    while index < value.len() {
        value[index] = (index as u16 + 1) * 7;
        index += 1;
    }
    value
}
const DATA: &[u16; 4] = &build();
const ALIAS: &[u16; 4] = DATA;
fn main() {
    assert_eq!(*DATA, [7, 14, 21, 28]);
    assert_eq!(*ALIAS, *DATA);
    let data = DATA;
    let alias = data;
    assert!(core::ptr::eq(data, alias));
}
