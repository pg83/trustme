trait Length { fn length(&self) -> usize; }
impl Length for &[u8] { fn length(&self) -> usize { self.len() } }
fn erase<T: Length>(value: &T) -> &dyn Length { value }
fn main() {
    let slice: &[u8] = &[17, 23, 31];
    let object = erase(&slice);
    assert_eq!(object.length(), 3);
    assert_eq!(std::mem::size_of_val(object), std::mem::size_of::<&[u8]>());
}
