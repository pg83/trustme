struct Pair<A, B>(A, B);
impl<T> Pair<T, T> {
    fn split(self) -> (T, T) { (self.0, self.1) }
}
fn main() {
    let (first, second) = Pair(vec![17u16], Vec::new()).split();
    assert_eq!(first, [17u16]);
    assert!(second.is_empty());
    let (first, second) = Pair(Some(2.5f64), None).split();
    assert_eq!(first, Some(2.5f64));
    assert_eq!(second, None);
    let (first, second) = Pair::<Vec<u32>, _>::split(Pair(vec![9], Vec::new()));
    assert_eq!(first, [9u32]);
    assert!(second.is_empty());
    let (first, second) = Pair([4.5f32], [6.25]).split();
    assert_eq!(first, [4.5f32]);
    assert_eq!(second, [6.25f32]);
    assert_eq!(Pair(vec![17u16], vec![9]).split(), (vec![17u16], vec![9u16]));
    assert_eq!(Pair(Some(2.5f64), Some(3.25)).split(), (Some(2.5f64), Some(3.25f64)));
    assert_eq!(Pair(vec![5], vec![23u32]).split(), (vec![5u32], vec![23u32]));
    assert_eq!(Pair(Some(4.5), Some(6.25f32)).split(), (Some(4.5f32), Some(6.25f32)));
    assert_eq!(Pair(Vec::new(), vec![7i16]).split(), (Vec::<i16>::new(), vec![7i16]));
    assert_eq!(Pair(None, Some(8.5f32)).split(), (None::<f32>, Some(8.5f32)));
}
