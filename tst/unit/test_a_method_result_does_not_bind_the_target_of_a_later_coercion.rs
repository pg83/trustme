// object 0.36's `StringTable::write` (thorin-dwp builds it):
//     let mut previous = &[][..];
//     for id in ids {
//         let string = self.strings.get_index(id).unwrap();   // &&'a [u8]
//         ... previous = string;
// `previous` is `&<RangeFull as SliceIndex<[?T]>>::Output`, and the assignment
// coerces `&&[u8]` into it: auto-deref gives `?T = u8`. The method call's
// result was related to that later assignment's target as an expected result,
// and the relation was kept - `&&[u8]` against `&_` bound the index output to
// `&[u8]` before the projection was normalized, and typeck ended with a rule it
// could not discharge. Upstream an expected output only guides a call
// (`expected_inputs_for_expected_output` runs under `fudge_inference_if_ok`,
// which throws away what it did to variables that existed before); the call's
// own result is still a coercion source.
struct Set<K> {
    items: Vec<K>,
}
impl<K> Set<K> {
    fn get_index(&self, index: usize) -> Option<&K> {
        self.items.get(index)
    }
    fn len(&self) -> usize {
        self.items.len()
    }
}
struct StringTable<'a> {
    strings: Set<&'a [u8]>,
    offsets: Vec<usize>,
}
impl<'a> StringTable<'a> {
    fn write(&mut self, base: usize, w: &mut Vec<u8>) {
        assert!(self.offsets.is_empty());
        let ids: Vec<_> = (0..self.strings.len()).collect();
        self.offsets = vec![0; ids.len()];
        let mut offset = base;
        let mut previous = &[][..];
        for id in ids {
            let string = self.strings.get_index(id).unwrap();
            if previous.ends_with(string) {
                self.offsets[id] = offset - string.len() - 1;
            } else {
                self.offsets[id] = offset;
                w.extend_from_slice(string);
                w.push(0);
                offset += string.len() + 1;
                previous = string;
            }
        }
    }
}
fn main() {
    let mut t = StringTable { strings: Set { items: vec![b"ab" as &[u8], b"b", b"cd"] }, offsets: Vec::new() };
    let mut w = Vec::new();
    t.write(1, &mut w);
    assert_eq!(t.offsets, vec![1, 2, 4]);
    assert_eq!(w, b"ab\0cd\0");
}
